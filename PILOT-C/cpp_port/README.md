# PILOT-C C++ Port

This folder contains a C++ reference port of the main `PILOT-C` compressor.

The implementation keeps PILOT-C's internal coordinate system metric. For raw longitude/latitude datasets such as T-drive, use `--input-format tdrive`; the loader projects points to a local meter-based plane before compression.

## Build

```powershell
cd D:\all_traj_method\PILOT-C\cpp_port
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

## Simple Meter Input

Input format:

```text
timestamp,x,y
timestamp,x,y
```

Run with a meter threshold:

```powershell
.\build\pilotc_reference.exe --input ..\workspace\datasets\test_scalability\traj_demo.txt --data-source test_scalability --max-error 1.0
```

## T-drive Raw Input

Raw T-drive format:

```text
taxi_id,yyyy-mm-dd HH:MM:SS,longitude,latitude
```

Run with a degree threshold written in scientific notation:

```powershell
.\build\pilotc_reference.exe --input ..\..\data\T-drive\1.txt --input-format tdrive --data-source geolife --max-error-degree 1e-5 --output .\build\tdrive_1_deg_1e-5.bin
```

`--max-error-degree` is converted to meters using the maximum of:

- meters per latitude degree
- meters per longitude degree at the trajectory reference latitude

For Beijing-area T-drive data, `1e-5` degree is about `1.11 m` by this conservative conversion.

## Important Parameter Meaning

- `--max-error`: metric error threshold in meters.
- `--max-error-degree`: longitude/latitude threshold in degrees; only valid for degree-based input such as `--input-format tdrive`.
- `--accuracy`: internal quantization ratio relative to `max_error`.
- `--block-size`, `--scale-length`, `--epsilon`: PILOT-C tuning parameters that are still defined after conversion in the internal meter-based space.

The algorithm itself should not be run directly on raw lon/lat values unless all speed checks and tuning parameters are recalibrated for degree units.
