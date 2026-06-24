"""
Inverse Kinematic Wave Routing

This script back-calculates Q (streamflow) values at all pixels given the Q at the basin outlet.
It builds the routing network from DEM, Flow Direction, and Flow Accumulation data.

Usage:
    # Option 1: Command-line arguments (simplest)
    python inverse_kw_routing.py <dem_file> <flow_dir_file> <flow_accum_file> <outlet_lon> <outlet_lat> <outlet_q_file> <runoff_dir> <output_dir> [alpha0] [alpha] [beta] [stepHours] [channel_threshold]
    
    # Option 2: CSV config file (easy to edit)
    python inverse_kw_routing.py --config <config.csv> <outlet_q_file> <runoff_dir> <output_dir>
    
Arguments (Option 1):
    dem_file: Path to DEM GeoTIFF
    flow_dir_file: Path to Flow Direction GeoTIFF
    flow_accum_file: Path to Flow Accumulation GeoTIFF
    outlet_lon, outlet_lat: Outlet coordinates (longitude, latitude in decimal degrees) - Preferred
    OR outlet_x, outlet_y: Outlet grid coordinates (pixel coordinates) - backward compatibility
    outlet_q_file: CSV file with outlet Q values (timestep, Q_m3s)
    runoff_dir: Directory containing runoff GeoTIFF files
    output_dir: Output directory for Q GeoTIFF files
    [alpha0]: Overland routing coefficient (default: 1.5)
    [alpha]: Channel routing coefficient (default: 2.0)
    [beta]: Channel routing exponent (default: 0.6)
    [stepHours]: Timestep in hours (default: 1.0)
    [channel_threshold]: Flow accumulation threshold for channels (default: 100.0)
"""

import numpy as np
import json
import sys
import os
import csv
from typing import Dict, List, Tuple, Optional
from collections import deque
import rasterio
from rasterio.transform import from_bounds
from rasterio.crs import CRS
import math


# Flow direction encoding (matching EF5 Defines.h)
FLOW_EAST = 1
FLOW_NORTHEAST = 2
FLOW_NORTH = 3
FLOW_NORTHWEST = 4
FLOW_WEST = 5
FLOW_SOUTHWEST = 6
FLOW_SOUTH = 7
FLOW_SOUTHEAST = 8
FLOW_QTY = 9

# Flow direction offsets (dx, dy)
FLOW_OFFSETS = {
    FLOW_EAST: (1, 0),
    FLOW_NORTHEAST: (1, -1),
    FLOW_NORTH: (0, -1),
    FLOW_NORTHWEST: (-1, -1),
    FLOW_WEST: (-1, 0),
    FLOW_SOUTHWEST: (-1, 1),
    FLOW_SOUTH: (0, 1),
    FLOW_SOUTHEAST: (1, 1),
}

# Reverse flow directions (for finding upstream)
REVERSE_FLOW = {
    FLOW_EAST: FLOW_WEST,
    FLOW_NORTHEAST: FLOW_SOUTHWEST,
    FLOW_NORTH: FLOW_SOUTH,
    FLOW_NORTHWEST: FLOW_SOUTHEAST,
    FLOW_WEST: FLOW_EAST,
    FLOW_SOUTHWEST: FLOW_NORTHEAST,
    FLOW_SOUTH: FLOW_NORTH,
    FLOW_SOUTHEAST: FLOW_NORTHWEST,
}


class Node:
    """Represents a grid node in the routing network"""
    def __init__(self, x, y, dem_value, flow_dir, flow_accum, area, contrib_area, 
                 hor_len, slope, is_channel=False):
        self.x = x
        self.y = y
        self.dem_value = dem_value
        self.flow_dir = flow_dir
        self.flow_accum = flow_accum
        self.area = area  # km²
        self.contrib_area = contrib_area  # km²
        self.hor_len = hor_len  # meters
        self.slope = slope
        self.is_channel = is_channel
        self.downstream_node_idx = None
        self.upstream_node_indices = []
        self.index = None


class InverseKWRoute:
    """
    Inverse Kinematic Wave Routing Calculator
    
    Builds routing network from DEM, Flow Direction, and Flow Accumulation,
    then back-calculates Q values from outlet upstream.
    """
    
    def __init__(self, dem_file: str, flow_dir_file: str, flow_accum_file: str,
                 outlet_lon: float, outlet_lat: float, routing_params: Dict):
        """
        Initialize inverse routing from GeoTIFF files
        
        Args:
            dem_file: Path to DEM GeoTIFF
            flow_dir_file: Path to Flow Direction GeoTIFF
            flow_accum_file: Path to Flow Accumulation GeoTIFF
            outlet_lon, outlet_lat: Outlet coordinates (longitude, latitude)
            routing_params: Dictionary with routing parameters
            
        Note: The script automatically searches nearby pixels to find the location
        with highest flow accumulation, handling projection displacement.
        """
        print("Loading DEM, Flow Direction, and Flow Accumulation...")
        
        # Load GeoTIFF files
        with rasterio.open(dem_file) as src:
            self.dem = src.read(1).astype(np.float32)
            self.dem_profile = src.profile
            self.dem_transform = src.transform
            self.dem_crs = src.crs
            self.nodata_dem = src.nodata if src.nodata is not None else -9999.0
        
        with rasterio.open(flow_dir_file) as src:
            self.flow_dir = src.read(1).astype(np.int32)
            if not np.array_equal(self.flow_dir.shape, self.dem.shape):
                raise ValueError("Flow Direction grid size doesn't match DEM")
        
        with rasterio.open(flow_accum_file) as src:
            self.flow_accum = src.read(1).astype(np.float32)
            if not np.array_equal(self.flow_accum.shape, self.dem.shape):
                raise ValueError("Flow Accumulation grid size doesn't match DEM")
        
        self.num_rows, self.num_cols = self.dem.shape
        self.cell_size = abs(self.dem_transform[0])  # meters
        
        # Convert outlet lat/lon to grid coordinates
        initial_x, initial_y = self._lonlat_to_xy(outlet_lon, outlet_lat)
        
        # Search for highest flow accumulation in nearby pixels (handles projection displacement)
        self.outlet_x, self.outlet_y = self._find_best_outlet_location(
            initial_x, initial_y, outlet_lon, outlet_lat
        )
        
        print(f"Outlet coordinates: lon={outlet_lon:.6f}, lat={outlet_lat:.6f}")
        print(f"Initial grid coordinates: x={initial_x}, y={initial_y}")
        print(f"Best outlet location (highest flow accum): x={self.outlet_x}, y={self.outlet_y}")
        if self.outlet_x != initial_x or self.outlet_y != initial_y:
            print(f"  (Adjusted by {self.outlet_x - initial_x}, {self.outlet_y - initial_y} pixels)")
        
        # Store parameters
        self.params = routing_params
        
        # Build node network
        print("Building routing network...")
        self.nodes = self._build_network()
        
        # Find outlet node
        self.outlet_index = self._find_outlet_node()
        if self.outlet_index is None:
            raise ValueError(f"Outlet at ({outlet_x}, {outlet_y}) not found in network")
        
        print(f"Network built: {len(self.nodes)} nodes")
        print(f"Outlet node index: {self.outlet_index}")
        
        # Initialize Q array
        self.Q = np.zeros(len(self.nodes))
    
    def _calculate_hor_len(self, x: int, y: int, flow_dir: int) -> float:
        """
        Calculate horizontal flow length based on flow direction
        Accounts for latitude (simplified geographic projection)
        """
        if flow_dir < 1 or flow_dir >= FLOW_QTY:
            return self.cell_size
        
        # Get cell center coordinates (simplified - would need actual lat/lon)
        # For now, use cell size directly
        dx, dy = FLOW_OFFSETS[flow_dir]
        
        if dx == 0 or dy == 0:
            # Cardinal direction
            return self.cell_size
        else:
            # Diagonal direction
            return self.cell_size * math.sqrt(2.0)
    
    def _calculate_slope(self, x: int, y: int, flow_dir: int) -> float:
        """Calculate slope from DEM"""
        if flow_dir < 1 or flow_dir >= FLOW_QTY:
            return 0.001  # Default slope
        
        dx, dy = FLOW_OFFSETS[flow_dir]
        next_x = x + dx
        next_y = y + dy
        
        if (next_x < 0 or next_x >= self.num_cols or 
            next_y < 0 or next_y >= self.num_rows):
            return 0.001
        
        current_elev = self.dem[y, x]
        next_elev = self.dem[next_y, next_x]
        
        if (np.isnan(current_elev) or np.isnan(next_elev) or
            current_elev == self.nodata_dem or next_elev == self.nodata_dem):
            return 0.001
        
        hor_len = self._calculate_hor_len(x, y, flow_dir)
        elev_diff = current_elev - next_elev
        
        if elev_diff < 1.0:
            elev_diff = 1.0  # Minimum 1m difference
        
        return elev_diff / hor_len
    
    def _lonlat_to_xy(self, lon: float, lat: float) -> Tuple[int, int]:
        """
        Convert longitude/latitude to grid pixel coordinates
        
        Args:
            lon: Longitude
            lat: Latitude
            
        Returns:
            (x, y) pixel coordinates
        """
        # Use rasterio's rowcol function to convert lon/lat to pixel coordinates
        # rowcol returns (row, col) but we need (x, y) = (col, row)
        row, col = rasterio.transform.rowcol(self.dem_transform, lon, lat)
        
        # Clamp to valid range
        x = max(0, min(col, self.num_cols - 1))
        y = max(0, min(row, self.num_rows - 1))
        
        return (x, y)
    
    def _find_best_outlet_location(self, initial_x: int, initial_y: int, 
                                   lon: float, lat: float) -> Tuple[int, int]:
        """
        Find best outlet location by searching for highest flow accumulation
        in nearby pixels (handles projection displacement)
        
        Based on GaugeMap logic: searches in spiral pattern for highest FAM value
        
        Args:
            initial_x, initial_y: Initial grid coordinates from lat/lon conversion
            lon, lat: Original coordinates (for calculating search distance)
            
        Returns:
            (x, y) pixel coordinates of best outlet location
        """
        # Check if initial location is valid
        if (initial_y < 0 or initial_y >= self.num_rows or 
            initial_x < 0 or initial_x >= self.num_cols):
            return (initial_x, initial_y)
        
        # Get initial flow accumulation value
        initial_fam = self.flow_accum[initial_y, initial_x]
        if np.isnan(initial_fam) or initial_fam == self.nodata_dem:
            initial_fam = 0.0
        
        # Calculate maximum search distance (similar to GaugeMap)
        # Search up to ~20km or 50 pixels, whichever is smaller
        cell_size_m = self.cell_size
        max_dist_m = 20000.0  # 20 km
        max_dist_pixels = int(max_dist_m / cell_size_m) if cell_size_m > 0 else 50
        max_dist_pixels = min(max_dist_pixels, 50)  # Cap at 50 pixels
        if max_dist_pixels < 2:
            max_dist_pixels = 2
        
        best_x = initial_x
        best_y = initial_y
        max_fam = initial_fam
        
        # Search in spiral pattern (8 directions) at increasing distances
        # Pattern: E, SE, S, SW, W, NW, N, NE
        for dist in range(1, max_dist_pixels + 1):
            # Check 8 directions
            directions = [
                (dist, 0),      # East
                (dist, dist),   # Southeast
                (0, dist),      # South
                (-dist, dist),  # Southwest
                (-dist, 0),     # West
                (-dist, -dist), # Northwest
                (0, -dist),     # North
                (dist, -dist)   # Northeast
            ]
            
            for dx, dy in directions:
                test_x = initial_x + dx
                test_y = initial_y + dy
                
                # Check bounds
                if (test_x < 0 or test_x >= self.num_cols or
                    test_y < 0 or test_y >= self.num_rows):
                    continue
                
                # Check if valid data
                fam_value = self.flow_accum[test_y, test_x]
                if np.isnan(fam_value) or fam_value == self.nodata_dem:
                    continue
                
                # Check if this is the highest flow accumulation so far
                if fam_value > max_fam:
                    max_fam = fam_value
                    best_x = test_x
                    best_y = test_y
        
        return (best_x, best_y)
    
    def _calculate_area(self, x: int, y: int) -> float:
        """Calculate cell area in km²"""
        # Simplified: assumes square cells
        area_m2 = self.cell_size * self.cell_size
        return area_m2 / 1e6  # Convert to km²
    
    def _is_channel_cell(self, flow_accum: float) -> bool:
        """Determine if cell is a channel based on flow accumulation threshold"""
        threshold = self.params.get('channel_threshold', 100.0)
        return flow_accum >= threshold
    
    def _build_network(self) -> List[Node]:
        """Build routing network from DEM, flow direction, and flow accumulation"""
        nodes = []
        node_map = {}  # Map from (x, y) to node index
        
        # First pass: create all nodes
        for y in range(self.num_rows):
            for x in range(self.num_cols):
                if (np.isnan(self.dem[y, x]) or self.dem[y, x] == self.nodata_dem):
                    continue
                
                flow_dir_val = int(self.flow_dir[y, x])
                if flow_dir_val < 1 or flow_dir_val >= FLOW_QTY:
                    continue
                
                flow_accum_val = self.flow_accum[y, x]
                if np.isnan(flow_accum_val) or flow_accum_val < 0:
                    continue
                
                # Calculate node properties
                area = self._calculate_area(x, y)
                hor_len = self._calculate_hor_len(x, y, flow_dir_val)
                slope = self._calculate_slope(x, y, flow_dir_val)
                is_channel = self._is_channel_cell(flow_accum_val)
                
                node = Node(
                    x=x, y=y,
                    dem_value=self.dem[y, x],
                    flow_dir=flow_dir_val,
                    flow_accum=flow_accum_val,
                    area=area,
                    contrib_area=area,  # Will be updated later
                    hor_len=hor_len,
                    slope=slope,
                    is_channel=is_channel
                )
                node.index = len(nodes)
                nodes.append(node)
                node_map[(x, y)] = node.index
        
        # Second pass: build downstream connections
        for node in nodes:
            dx, dy = FLOW_OFFSETS[node.flow_dir]
            next_x = node.x + dx
            next_y = node.y + dy
            
            if (next_x, next_y) in node_map:
                node.downstream_node_idx = node_map[(next_x, next_y)]
                nodes[node.downstream_node_idx].upstream_node_indices.append(node.index)
        
        # Third pass: calculate contributing areas (accumulate upstream)
        # Sort nodes by flow accumulation (lowest to highest)
        sorted_indices = sorted(range(len(nodes)), 
                               key=lambda i: nodes[i].flow_accum)
        
        for idx in sorted_indices:
            node = nodes[idx]
            # Contributing area = own area + sum of upstream contributing areas
            for up_idx in node.upstream_node_indices:
                node.contrib_area += nodes[up_idx].contrib_area
        
        return nodes
    
    def _find_outlet_node(self) -> Optional[int]:
        """Find outlet node (node at outlet coordinates)"""
        for node in self.nodes:
            if node.x == self.outlet_x and node.y == self.outlet_y:
                return node.index
        return None
    
    def solve_inverse_overland(self, node_idx: int, Q_out: float, 
                              step_seconds: float, local_runoff: float = 0.0) -> float:
        """
        Solve inverse kinematic wave equation for overland flow
        
        Forward: Q = α * q^β where q is flow per unit width (cms/m)
        For overland: Q_out = q_out * horLen
        
        Args:
            node_idx: Current node index
            Q_out: Outgoing flow (m³/s)
            step_seconds: Timestep in seconds
            local_runoff: Local runoff contribution (mm/timestep)
            
        Returns:
            Q_in: Incoming flow from upstream (m³/s)
        """
        node = self.nodes[node_idx]
        
        if node.hor_len <= 0:
            return 0.0
        
        # Convert Q_out to flow per unit width (cms/m)
        q_out = Q_out / node.hor_len
        
        # Get routing parameters
        alpha = self.params.get('alpha0', 1.0)
        beta = 0.6  # Fixed for overland
        
        # Account for local runoff
        # Local runoff in mm/timestep -> convert to m³/s
        local_q = 0.0
        if local_runoff > 0:
            # Convert mm to m, then to m³/s
            local_volume_m3 = (local_runoff / 1000.0) * (node.area * 1e6)  # m³
            local_q = local_volume_m3 / step_seconds  # m³/s
            local_q_per_width = local_q / node.hor_len  # cms/m
        
        # Simplified inverse: q_in ≈ q_out - local_input
        q_in = max(0.0, q_out - local_q_per_width)
        
        # Convert back to total flow (m³/s)
        Q_in = q_in * node.hor_len
        
        return max(0.0, Q_in)
    
    def solve_inverse_channel(self, node_idx: int, Q_out: float,
                             step_seconds: float, local_runoff: float = 0.0) -> float:
        """
        Solve inverse kinematic wave equation for channel flow
        
        Forward: Q = α * q^β
        For channel: Q is directly in cms (m³/s)
        
        Args:
            node_idx: Current node index
            Q_out: Outgoing flow (m³/s)
            step_seconds: Timestep in seconds
            local_runoff: Local runoff contribution (mm/timestep)
            
        Returns:
            Q_in: Incoming flow from upstream (m³/s)
        """
        node = self.nodes[node_idx]
        
        # Get routing parameters
        alpha = self.params.get('alpha', 1.0)
        beta = self.params.get('beta', 0.6)
        
        if alpha <= 0 or beta <= 0:
            return 0.0
        
        # Account for local runoff
        local_q = 0.0
        if local_runoff > 0:
            # Convert mm to m, then to m³/s
            local_volume_m3 = (local_runoff / 1000.0) * (node.area * 1e6)  # m³
            local_q = local_volume_m3 / step_seconds  # m³/s
        
        # Simplified inverse: Q_in ≈ Q_out - local_input
        Q_in = max(0.0, Q_out - local_q)
        
        return Q_in
    
    def distribute_q_by_area(self, node_idx: int, total_q: float,
                             upstream_indices: List[int]) -> Dict[int, float]:
        """Distribute Q to upstream nodes based on contributing area"""
        if not upstream_indices:
            return {}
        
        # Get contributing areas
        areas = [self.nodes[up_idx].contrib_area for up_idx in upstream_indices]
        total_area = sum(areas)
        
        if total_area <= 0:
            # Equal distribution if no area info
            q_per_node = total_q / len(upstream_indices)
            return {idx: q_per_node for idx in upstream_indices}
        
        # Distribute proportionally
        distributed = {}
        for i, up_idx in enumerate(upstream_indices):
            fraction = areas[i] / total_area
            distributed[up_idx] = total_q * fraction
        
        return distributed
    
    def back_calculate_q(self, outlet_q: float, step_hours: float,
                         runoff_data: Optional[np.ndarray] = None) -> np.ndarray:
        """
        Back-calculate Q values at all pixels given outlet Q
        
        Args:
            outlet_q: Streamflow at outlet (m³/s)
            step_hours: Timestep in hours
            runoff_data: Optional 2D array of runoff (mm/timestep) matching DEM grid
            
        Returns:
            Array of Q values (m³/s) for each node
        """
        step_seconds = step_hours * 3600.0
        
        # Initialize
        self.Q = np.zeros(len(self.nodes))
        processed = np.zeros(len(self.nodes), dtype=bool)
        
        # Set outlet Q
        self.Q[self.outlet_index] = outlet_q
        processed[self.outlet_index] = True
        
        # BFS traversal from outlet upstream
        queue = deque([self.outlet_index])
        
        while queue:
            current_idx = queue.popleft()
            
            if not processed[current_idx]:
                continue
            
            # Get upstream nodes
            upstream_indices = self.nodes[current_idx].upstream_node_indices
            
            if not upstream_indices:
                continue  # No upstream nodes (headwater)
            
            current_q = self.Q[current_idx]
            node = self.nodes[current_idx]
            
            # Get local runoff if available
            local_runoff = 0.0
            if runoff_data is not None:
                if (node.y < runoff_data.shape[0] and 
                    node.x < runoff_data.shape[1]):
                    local_runoff = runoff_data[node.y, node.x]
                    if np.isnan(local_runoff):
                        local_runoff = 0.0
            
            # Determine if channel or overland
            if node.is_channel:
                Q_in = self.solve_inverse_channel(
                    current_idx, current_q, step_seconds, local_runoff
                )
            else:
                Q_in = self.solve_inverse_overland(
                    current_idx, current_q, step_seconds, local_runoff
                )
            
            # Distribute to upstream nodes
            if len(upstream_indices) == 1:
                # Single upstream node gets all
                self.Q[upstream_indices[0]] = Q_in
                processed[upstream_indices[0]] = True
                queue.append(upstream_indices[0])
            else:
                # Multiple upstream nodes - distribute by area
                distributed = self.distribute_q_by_area(
                    current_idx, Q_in, upstream_indices
                )
                for up_idx, q_val in distributed.items():
                    self.Q[up_idx] += q_val
                    if not processed[up_idx]:
                        processed[up_idx] = True
                        queue.append(up_idx)
        
        return self.Q
    
    def save_to_geotiff(self, output_file: str, q_values: np.ndarray):
        """
        Save Q values to GeoTIFF file
        
        Args:
            output_file: Output file path
            q_values: Q values for each node (m³/s)
        """
        # Create output grid matching DEM
        output_grid = np.full((self.num_rows, self.num_cols), 
                             self.nodata_dem, dtype=np.float32)
        
        # Fill in Q values at node locations
        for node in self.nodes:
            output_grid[node.y, node.x] = q_values[node.index]
        
        # Write GeoTIFF
        profile = self.dem_profile.copy()
        profile.update({
            'dtype': 'float32',
            'nodata': self.nodata_dem,
            'compress': 'lzw'
        })
        
        with rasterio.open(output_file, 'w', **profile) as dst:
            dst.write(output_grid, 1)
            dst.update_tags(
                Description='Back-calculated streamflow (Q) from outlet',
                Units='m3/s'
            )


def parse_timestep(timestep_str: str) -> Tuple[int, int, int, int, int]:
    """
    Parse timestep string to (year, month, day, hour, minute)
    Supports formats: YYYYMMDD_HHMM, YYYY-MM-DD HH:MM, YYYYMMDDHHMM, etc.
    """
    # Remove common separators
    clean = timestep_str.replace('-', '').replace('_', '').replace(' ', '').replace(':', '')
    
    if len(clean) >= 10:
        # YYYYMMDDHH or YYYYMMDDHHMM
        year = int(clean[0:4])
        month = int(clean[4:6])
        day = int(clean[6:8])
        hour = int(clean[8:10]) if len(clean) >= 10 else 0
        minute = int(clean[10:12]) if len(clean) >= 12 else 0
        return (year, month, day, hour, minute)
    else:
        raise ValueError(f"Cannot parse timestep: {timestep_str}")


def timestep_to_minutes(year: int, month: int, day: int, hour: int, minute: int) -> int:
    """Convert timestep to total minutes since epoch (simplified)"""
    # Simplified: just use as relative time
    return year * 525600 + month * 43800 + day * 1440 + hour * 60 + minute


def find_nearest_runoff_timestep(target_timestep: str, available_runoff_timesteps: List[str]) -> Optional[str]:
    """Find nearest available runoff timestep to target timestep"""
    if not available_runoff_timesteps:
        return None
    
    try:
        target_parsed = parse_timestep(target_timestep)
        target_minutes = timestep_to_minutes(*target_parsed)
        
        best_match = None
        min_diff = float('inf')
        
        for runoff_ts in available_runoff_timesteps:
            try:
                runoff_parsed = parse_timestep(runoff_ts)
                runoff_minutes = timestep_to_minutes(*runoff_parsed)
                diff = abs(target_minutes - runoff_minutes)
                
                if diff < min_diff:
                    min_diff = diff
                    best_match = runoff_ts
            except:
                continue
        
        return best_match
    except:
        # Fallback: return first available if parsing fails
        return available_runoff_timesteps[0] if available_runoff_timesteps else None


def load_outlet_q_timeseries(outlet_q_file: str) -> Dict[str, float]:
    """Load outlet Q timeseries from CSV file"""
    timeseries = {}
    with open(outlet_q_file, 'r') as f:
        reader = csv.DictReader(f)
        for row in reader:
            timestep = row.get('timestep', row.get('time', ''))
            q_value = float(row.get('Q_m3s', row.get('Q', 0.0)))
            timeseries[timestep] = q_value
    return timeseries


def get_available_runoff_timesteps(runoff_dir: str) -> List[str]:
    """Get list of available runoff timesteps from filenames"""
    if not os.path.exists(runoff_dir):
        return []
    
    timesteps = []
    for filename in os.listdir(runoff_dir):
        if filename.startswith('runoff_') and filename.endswith('.tif'):
            # Extract timestep from filename: runoff_YYYYMMDD_HHMM.tif
            timestep = filename[7:-4]  # Remove 'runoff_' prefix and '.tif' suffix
            timesteps.append(timestep)
    
    return sorted(timesteps)


def load_runoff_geotiff(runoff_file: str) -> Optional[np.ndarray]:
    """Load runoff GeoTIFF file"""
    try:
        with rasterio.open(runoff_file) as src:
            return src.read(1).astype(np.float32)
    except Exception as e:
        print(f"Warning: Could not load runoff file {runoff_file}: {e}")
        return None


def load_config_csv(config_file: str) -> Dict:
    """Load configuration from CSV file"""
    config = {}
    with open(config_file, 'r') as f:
        reader = csv.DictReader(f)
        for row in reader:
            key = row.get('parameter', '').strip()
            value = row.get('value', '').strip()
            if key and value:
                # Try to convert to appropriate type
                if key in ['outlet_x', 'outlet_y']:
                    config[key] = int(value)
                elif key in ['alpha0', 'alpha', 'beta', 'stepHours', 'channel_threshold', 'runoff_timestep_hours']:
                    config[key] = float(value)
                else:
                    config[key] = value
    return config


def main():
    """Main function"""
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    
    # Check if using CSV config file
    if sys.argv[1] == '--config' or sys.argv[1] == '-c':
        if len(sys.argv) < 5:
            print("Usage: python inverse_kw_routing.py --config <config.csv> <outlet_q_file> <runoff_dir> <output_dir>")
            sys.exit(1)
        
        config_file = sys.argv[2]
        outlet_q_file = sys.argv[3]
        runoff_dir = sys.argv[4]
        output_dir = sys.argv[5]
        
        # Load configuration from CSV
        print(f"Loading configuration from {config_file}...")
        config = load_config_csv(config_file)
        
        # Get file paths
        dem_file = config.get('dem_file')
        flow_dir_file = config.get('flow_dir_file')
        flow_accum_file = config.get('flow_accum_file')
        
        # Get outlet coordinates (prefer lat/lon, fallback to x/y for backward compatibility)
        outlet_lon = config.get('outlet_lon')
        outlet_lat = config.get('outlet_lat')
        outlet_x = config.get('outlet_x')
        outlet_y = config.get('outlet_y')
        
        # Validate outlet coordinates
        if outlet_lon is not None and outlet_lat is not None:
            outlet_coords = (float(outlet_lon), float(outlet_lat))
            use_latlon = True
        elif outlet_x is not None and outlet_y is not None:
            outlet_coords = (int(outlet_x), int(outlet_y))
            use_latlon = False
        else:
            print("Error: Must provide either (outlet_lon, outlet_lat) or (outlet_x, outlet_y)")
            sys.exit(1)
        
        routing_params = {
            'alpha0': config.get('alpha0', 1.5),
            'alpha': config.get('alpha', 2.0),
            'beta': config.get('beta', 0.6),
            'stepHours': config.get('stepHours', 1.0),
            'channel_threshold': config.get('channel_threshold', 100.0),
            'runoff_timestep_hours': config.get('runoff_timestep_hours', None)
        }
        
        # Validate required fields
        if not all([dem_file, flow_dir_file, flow_accum_file]):
            print("Error: Missing required parameters in config file:")
            print("  Required: dem_file, flow_dir_file, flow_accum_file")
            print("  And either: outlet_lon, outlet_lat (preferred)")
            print("  Or: outlet_x, outlet_y (grid coordinates)")
            sys.exit(1)
    
    else:
        # Command-line arguments
        if len(sys.argv) < 9:
            print(__doc__)
            sys.exit(1)
        
        dem_file = sys.argv[1]
        flow_dir_file = sys.argv[2]
        flow_accum_file = sys.argv[3]
        
        # Outlet coordinates - can be lat/lon (float) or x/y (int)
        # Try to parse as float first (lat/lon), if fails use as int (x/y)
        try:
            outlet_lon = float(sys.argv[4])
            outlet_lat = float(sys.argv[5])
            outlet_coords = (outlet_lon, outlet_lat)
            use_latlon = True
        except ValueError:
            outlet_x = int(sys.argv[4])
            outlet_y = int(sys.argv[5])
            outlet_coords = (outlet_x, outlet_y)
            use_latlon = False
        
        outlet_q_file = sys.argv[6]
        runoff_dir = sys.argv[7]
        output_dir = sys.argv[8]
        
        # Optional routing parameters
        routing_params = {
            'alpha0': float(sys.argv[9]) if len(sys.argv) > 9 else 1.5,
            'alpha': float(sys.argv[10]) if len(sys.argv) > 10 else 2.0,
            'beta': float(sys.argv[11]) if len(sys.argv) > 11 else 0.6,
            'stepHours': float(sys.argv[12]) if len(sys.argv) > 12 else 1.0,
            'channel_threshold': float(sys.argv[13]) if len(sys.argv) > 13 else 100.0,
            'runoff_timestep_hours': float(sys.argv[14]) if len(sys.argv) > 14 else None
        }
    
    # Initialize inverse routing
    print("Initializing inverse routing...")
    if use_latlon:
        router = InverseKWRoute(
            dem_file, flow_dir_file, flow_accum_file,
            outlet_coords[0], outlet_coords[1], routing_params
        )
    else:
        # For backward compatibility with x/y coordinates
        # We need to convert x/y to lat/lon first, then back to x/y
        # This ensures consistent behavior
        with rasterio.open(dem_file) as src:
            lon, lat = rasterio.transform.xy(src.transform, outlet_coords[1], outlet_coords[0])
        router = InverseKWRoute(
            dem_file, flow_dir_file, flow_accum_file,
            lon, lat, routing_params
        )
    
    # Load outlet Q timeseries
    print(f"Loading outlet Q timeseries from {outlet_q_file}...")
    outlet_q_ts = load_outlet_q_timeseries(outlet_q_file)
    
    # Get available runoff timesteps
    print(f"Scanning runoff directory: {runoff_dir}...")
    available_runoff_ts = get_available_runoff_timesteps(runoff_dir)
    
    if available_runoff_ts:
        print(f"Found {len(available_runoff_ts)} runoff files")
        print(f"  First: {available_runoff_ts[0]}")
        print(f"  Last: {available_runoff_ts[-1]}")
    else:
        print("Warning: No runoff files found. Processing without local runoff.")
    
    # Get timestep from config
    step_hours = routing_params.get('stepHours', 1.0)
    
    # Temporal resolution handling
    runoff_timestep_hours = routing_params.get('runoff_timestep_hours', None)
    if runoff_timestep_hours is None and available_runoff_ts:
        # Try to infer from available timesteps
        if len(available_runoff_ts) >= 2:
            try:
                ts1 = parse_timestep(available_runoff_ts[0])
                ts2 = parse_timestep(available_runoff_ts[1])
                mins1 = timestep_to_minutes(*ts1)
                mins2 = timestep_to_minutes(*ts2)
                diff_minutes = abs(mins2 - mins1)
                runoff_timestep_hours = diff_minutes / 60.0
                print(f"Inferred runoff timestep: {runoff_timestep_hours:.2f} hours")
            except:
                runoff_timestep_hours = step_hours
        else:
            runoff_timestep_hours = step_hours
    
    # Create output directory
    os.makedirs(output_dir, exist_ok=True)
    
    # Process each timestep
    print(f"\nProcessing {len(outlet_q_ts)} outlet Q timesteps...")
    print(f"Outlet Q timestep: {step_hours:.2f} hours")
    if runoff_timestep_hours:
        print(f"Runoff timestep: {runoff_timestep_hours:.2f} hours")
    
    for timestep, outlet_q in sorted(outlet_q_ts.items()):
        print(f"\nProcessing timestep {timestep}, outlet Q = {outlet_q:.2f} m³/s")
        
        # Find matching runoff data
        runoff_data = None
        if available_runoff_ts:
            # Try exact match first
            runoff_file = os.path.join(runoff_dir, f"runoff_{timestep}.tif")
            if os.path.exists(runoff_file):
                runoff_data = load_runoff_geotiff(runoff_file)
                print(f"  Using exact match runoff file: runoff_{timestep}.tif")
            else:
                # Find nearest available runoff timestep
                nearest_ts = find_nearest_runoff_timestep(timestep, available_runoff_ts)
                if nearest_ts:
                    runoff_file = os.path.join(runoff_dir, f"runoff_{nearest_ts}.tif")
                    if os.path.exists(runoff_file):
                        runoff_data = load_runoff_geotiff(runoff_file)
                        print(f"  Using nearest runoff file: runoff_{nearest_ts}.tif (target: {timestep})")
        
        # Adjust step_hours for this timestep if runoff resolution differs
        # If runoff is hourly and outlet is 15-min, use outlet timestep for routing
        # but the runoff value represents the hourly average
        effective_step_hours = step_hours
        
        # Back-calculate Q values
        q_values = router.back_calculate_q(outlet_q, effective_step_hours, runoff_data)
        
        # Save to GeoTIFF
        output_file = os.path.join(output_dir, f"Q_{timestep}.tif")
        router.save_to_geotiff(output_file, q_values)
        print(f"  Saved to {output_file}")
    
    print(f"\nCompleted! Output files saved to {output_dir}")


if __name__ == '__main__':
    main()
