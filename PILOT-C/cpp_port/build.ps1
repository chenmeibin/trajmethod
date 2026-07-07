New-Item -ItemType Directory -Force -Path build | Out-Null
g++ -std=c++17 -O2 -Wall -Wextra -pedantic .\src\pilotc_reference.cpp -o .\build\pilotc_reference.exe
