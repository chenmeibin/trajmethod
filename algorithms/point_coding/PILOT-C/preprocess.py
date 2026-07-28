import os
import yaml
import pickle
import math
import numpy as np

PROJECT_ROOT = os.path.dirname(os.path.abspath(__file__))
DEFAULT_DATA_ROOT = os.environ.get('PRESS_DATA_ROOT', os.path.join(PROJECT_ROOT, 'workspace', 'datasets'))
GEOLIFE_RAW_ROOT = os.environ.get('GEOLIFE_RAW_ROOT', '/home/wkf/data/Geolife Trajectories 1.3/Data')
MOPSI_RAW_ROOT = os.environ.get('MOPSI_RAW_ROOT', '/home/wkf/data/mopsi_routes')

def get_data(line, title, name, default = np.float64(0)):
    if name in title:
        data = line[title.index(name)]
        if len(data):
            return np.float64(data)
        else:
            return default
    else:
        return default

from pyproj import Proj, Transformer
import multiprocessing

def reset_dir(path):
    import shutil
    if os.path.exists(path):
        shutil.rmtree(path)
    os.makedirs(path)

def lonlat_to_utm(lon, lat, utm_zone=50):
    utm_proj = Proj(proj='utm', zone=utm_zone, south=False)
    wgs_proj = Proj(proj='latlong', datum='WGS84')
    
    transformer = Transformer.from_proj(wgs_proj, utm_proj, always_xy=True)
    x, y = transformer.transform(lon, lat)
    return [x, y]

def geodetic_to_ecef(lon, lat, alt):
    a = 6378137.0
    e_sq = 0.00669437999014
    lambda_rad = math.radians(lon)
    phi_rad = math.radians(lat)
    
    sin_phi = math.sin(phi_rad)
    N = a / math.sqrt(1 - e_sq * sin_phi**2)
    
    x = (N + alt) * math.cos(phi_rad) * math.cos(lambda_rad)
    y = (N + alt) * math.cos(phi_rad) * math.sin(lambda_rad)
    z = (N * (1 - e_sq) + alt) * sin_phi
    
    return (x, y, z)
    
def process_file_geolife(ff, dir_name):
    all_data = []
    file_names = os.listdir(ff)
    file_names.sort()
    
    for file_name in file_names:
        temp_path = os.path.join(ff, file_name)
        data = [x.strip().split(",") for x in open(temp_path).readlines()]
        data = data[6:]
        all_data.extend(data)
        
    traj = []
    t_list = []
    for line in all_data:
        lat = np.float64(line[0])
        lon = np.float64(line[1])
        h = np.float64(line[3])
        t = round(np.float64(line[4]) * 86400)
        
        if len(t_list) == 0 or abs(t - t_list[-1]) > 1e-6:
            t_list.append(t)
            data = lonlat_to_utm(lon, lat, utm_zone=35)
            assert not math.isinf(data[0]) and not math.isinf(data[1])
            traj.append(data)
    traj = np.array(traj)
            
    other_points = []
    index_list = []
    
    start_index = 0
    end_index = 0
    while start_index < len(t_list):
        end_index = start_index
        while end_index < len(t_list) - 1 and \
                t_list[end_index + 1] - t_list[end_index] < 120 and \
                  np.sqrt(np.sum((traj[end_index + 1] - traj[end_index]) ** 2) / (t_list[end_index + 1] - t_list[end_index])) < 200:
            end_index = end_index + 1
        
        if end_index - start_index <= 0:
            for i in range(start_index, end_index + 1):
                other_points.append(i)
            start_index = end_index + 1
            continue
        index_list.append([start_index, end_index])
        start_index = end_index + 1
    
    
    traj_path = os.path.join(DEFAULT_DATA_ROOT, 'geolife', f"traj_{dir_name}.txt")
    with open(traj_path, "w") as f:
        for j in range(len(traj)):
            f.write(f"{t_list[j]},{traj[j][0]},{traj[j][1]}\n")
    print(ff, flush=True)
    
    return len(other_points)
        
def preprocess_geolife():
    reset_dir(os.path.join(DEFAULT_DATA_ROOT, 'geolife'))
    
    path = GEOLIFE_RAW_ROOT
    tot_path = []
    
    for dir_name in os.listdir(path):
        ff = os.path.join(path, dir_name)
        ff = os.path.join(ff, 'Trajectory')
        tot_path.append([ff, dir_name])
        
    with multiprocessing.Pool() as pool:
        results = []
        for ff, dir_name in tot_path:
            result = pool.apply_async(process_file_geolife, args=(ff, dir_name,))
            results.append(result)
        tot_cnt = 0
        for result in results:
            cnt = result.get()
            tot_cnt += cnt
    print(f"total other points: {tot_cnt}") # 649911
    
    print("preprocess geolife done")

def process_file_mopsi(ff, dir_name):
    all_data = []
    file_names = os.listdir(ff)
    file_names.sort()
    
    for file_name in file_names:
        temp_path = os.path.join(ff, file_name)
        data = [x.strip().split(" ") for x in open(temp_path).readlines()]
        all_data.extend(data)
        
    traj = []
    t_list = []
    for line in all_data:
        lat = np.float64(line[0])
        lon = np.float64(line[1])
        h = np.float64(line[3])
        t = np.float64(line[2]) / 1000
        
        if len(t_list) == 0 or abs(t - t_list[-1]) > 1e-6:
            assert len(t_list) == 0 or t > t_list[-1], t
            t_list.append(t)
            data = lonlat_to_utm(lon, lat, utm_zone=40)
            assert not math.isinf(data[0]) and not math.isinf(data[1])
            traj.append(data)
    traj = np.array(traj)
            
    other_points = []
    index_list = []
    
    start_index = 0
    end_index = 0
    while start_index < len(t_list):
        end_index = start_index
        while end_index < len(t_list) - 1 and \
                t_list[end_index + 1] - t_list[end_index] < 120 and \
                  np.sqrt(np.sum((traj[end_index + 1] - traj[end_index]) ** 2) / (t_list[end_index + 1] - t_list[end_index])) < 200:
            end_index = end_index + 1
        
        if end_index - start_index <= 10:
            for i in range(start_index, end_index + 1):
                other_points.append(i)
            start_index = end_index + 1
            continue
        index_list.append([start_index, end_index])
        start_index = end_index + 1
    
    
    traj_path = os.path.join(DEFAULT_DATA_ROOT, 'mopsi', f"traj_{dir_name}.txt")
    with open(traj_path, "w") as f:
        for j in range(len(traj)):
            f.write(f"{t_list[j]},{traj[j][0]},{traj[j][1]}\n")
    print(ff, flush=True)
    
    return len(other_points)
    
def preprocess_mopsi():
    reset_dir(os.path.join(DEFAULT_DATA_ROOT, 'mopsi'))
    
    path = MOPSI_RAW_ROOT
    tot_path = []
    
    for dir_name in os.listdir(path):
        ff = os.path.join(path, dir_name)
        tot_path.append([ff, dir_name])
        
    with multiprocessing.Pool() as pool:
        results = []
        for ff, dir_name in tot_path:
            result = pool.apply_async(process_file_mopsi, args=(ff, dir_name,))
            results.append(result)
        tot_cnt = 0
        for result in results:
            cnt = result.get()
            tot_cnt += cnt
    print(f"total other points: {tot_cnt}")
    
    print("preprocess mopsi done")
    
def process_file_geolife_3d(ff, dir_name):
    all_data = []
    file_names = os.listdir(ff)
    file_names.sort()
    
    for file_name in file_names:
        temp_path = os.path.join(ff, file_name)
        data = [x.strip().split(",") for x in open(temp_path).readlines()]
        data = data[6:]
        all_data.extend(data)
        
    traj = []
    t_list = []
    for line in all_data:
        lat = np.float64(line[0])
        lon = np.float64(line[1])
        h = np.float64(line[3]) * 0.3048
        t = round(np.float64(line[4]) * 86400)
        
        if len(t_list) == 0 or abs(t - t_list[-1]) > 1e-6:
            t_list.append(t)
            data = geodetic_to_ecef(lon, lat, h)
            traj.append(data)
    traj = np.array(traj)
            
    other_points = []
    index_list = []
    
    start_index = 0
    end_index = 0
    while start_index < len(t_list):
        end_index = start_index
        while end_index < len(t_list) - 1 and \
                t_list[end_index + 1] - t_list[end_index] < 120 and \
                  np.sqrt(np.sum((traj[end_index + 1] - traj[end_index]) ** 2) / (t_list[end_index + 1] - t_list[end_index])) < 200:
            end_index = end_index + 1
        
        if end_index - start_index <= 10:
            for i in range(start_index, end_index + 1):
                other_points.append(i)
            start_index = end_index + 1
            continue
        index_list.append([start_index, end_index])
        start_index = end_index + 1
    
    
    traj_path = os.path.join(DEFAULT_DATA_ROOT, 'geolife_3d', f"traj_{dir_name}.txt")
    with open(traj_path, "w") as f:
        for j in range(len(traj)):
            f.write(f"{t_list[j]},{traj[j][0]},{traj[j][1]},{traj[j][2]}\n")
    print(ff, flush=True)
    
    return len(other_points)
    
def preprocess_geolife_3d():
    reset_dir(os.path.join(DEFAULT_DATA_ROOT, 'geolife_3d'))
    
    path = GEOLIFE_RAW_ROOT
    tot_path = []
    
    for dir_name in os.listdir(path):
        ff = os.path.join(path, dir_name)
        ff = os.path.join(ff, 'Trajectory')
        tot_path.append([ff, dir_name])
        
    with multiprocessing.Pool() as pool:
        results = []
        for ff, dir_name in tot_path:
            result = pool.apply_async(process_file_geolife_3d, args=(ff, dir_name,))
            results.append(result)
        tot_cnt = 0
        for result in results:
            cnt = result.get()
            tot_cnt += cnt
    print(f"total other points: {tot_cnt}")
    
    print("preprocess geolife_3d done")

def process_file_test_scalability(ff, dir_name):
    all_data = []
    file_names = os.listdir(ff)
    file_names.sort()
    
    for file_name in file_names:
        temp_path = os.path.join(ff, file_name)
        data = [x.strip().split(" ") for x in open(temp_path).readlines()]
        all_data.extend(data)
        
    traj = []
    t_list = []
    for line in all_data:
        lat = np.float64(line[0])
        lon = np.float64(line[1])
        h = np.float64(line[3])
        t = np.float64(line[2]) / 1000
        
        if len(t_list) == 0 or abs(t - t_list[-1]) > 1e-6:
            assert len(t_list) == 0 or t > t_list[-1], t
            t_list.append(t)
            data = lonlat_to_utm(lon, lat, utm_zone=40)
            assert not math.isinf(data[0]) and not math.isinf(data[1])
            traj.append(data)
    traj = np.array(traj)
    if len(traj) < 10000:
        return 0
    
    for i in range(1000, 11000, 1000):
        traj_path = os.path.join(DEFAULT_DATA_ROOT, 'test_scalability', f"traj_{dir_name}_len_{i}.txt")
        with open(traj_path, "w") as f:
            for j in range(i):
                f.write(f"{t_list[j]},{traj[j][0]},{traj[j][1]}\n")
        print(traj_path)
    return 1
    
def preprocess_test_scalability():
    reset_dir(os.path.join(DEFAULT_DATA_ROOT, 'test_scalability'))
    
    path = MOPSI_RAW_ROOT
    tot_path = []
    
    for dir_name in os.listdir(path):
        ff = os.path.join(path, dir_name)
        tot_path.append([ff, dir_name])
        
    with multiprocessing.Pool() as pool:
        results = []
        for ff, dir_name in tot_path:
            result = pool.apply_async(process_file_test_scalability, args=(ff, dir_name,))
            results.append(result)
        tot = 0
        for result in results:
            tot += result.get()
    print(tot)
    print("preprocess test_scalability done")

if __name__ == '__main__':
    preprocess_test_scalability()
