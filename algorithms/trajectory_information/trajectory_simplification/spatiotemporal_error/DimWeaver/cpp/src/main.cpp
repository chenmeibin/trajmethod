#include "dimweaver/dimweaver.hpp"

#include <chrono>
#include <exception>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage: dimweaver_cli [--input-format csv|tdrive] <trajectory_file> <epsilon> <c>\n";
        return 1;
    }

    try {
        std::string input_format = "csv";
        int arg_index = 1;
        if (argc >= 6 && std::string(argv[1]) == "--input-format") {
            input_format = argv[2];
            arg_index = 3;
        }

        if (argc - arg_index < 3) {
            std::cerr << "Usage: dimweaver_cli [--input-format csv|tdrive] <trajectory_file> <epsilon> <c>\n";
            return 1;
        }

        const std::string path = argv[arg_index];
        const double epsilon = std::stod(argv[arg_index + 1]);
        const double c = std::stod(argv[arg_index + 2]);

        const std::vector<dimweaver::TrajectoryPoint> trajectory =
            (input_format == "tdrive") ? dimweaver::read_tdrive_trajectory(path)
                                       : dimweaver::read_csv_trajectory(path);
        const std::size_t original_bytes =
            trajectory.size() * (sizeof(std::int64_t) + sizeof(double) + sizeof(double));

        const auto compress_start = std::chrono::steady_clock::now();
        const std::vector<std::uint8_t> binary = dimweaver::compress(trajectory, epsilon, c);
        const auto compress_end = std::chrono::steady_clock::now();

        const auto decompress_start = std::chrono::steady_clock::now();
        const std::vector<std::vector<dimweaver::Line>> lines = dimweaver::decompress(binary);
        const auto decompress_end = std::chrono::steady_clock::now();

        const auto reconstruct_start = decompress_end;
        const std::vector<dimweaver::TrajectoryPoint> reconstructed =
            dimweaver::reconstruct_trajectory(lines, trajectory);
        const auto reconstruct_end = std::chrono::steady_clock::now();
        const double max_error = dimweaver::max_reconstruction_error(trajectory, reconstructed);
        const auto compress_us =
            std::chrono::duration_cast<std::chrono::microseconds>(compress_end - compress_start).count();
        const auto decompress_us =
            std::chrono::duration_cast<std::chrono::microseconds>(decompress_end - decompress_start).count();
        const auto reconstruct_us =
            std::chrono::duration_cast<std::chrono::microseconds>(reconstruct_end - reconstruct_start).count();
        const double compression_ratio =
            binary.empty() ? 0.0 : static_cast<double>(original_bytes) / static_cast<double>(binary.size());
        const double compressed_fraction =
            original_bytes == 0 ? 0.0 : static_cast<double>(binary.size()) / static_cast<double>(original_bytes);

        std::cout << std::fixed << std::setprecision(6);
        std::cout << "points=" << trajectory.size() << "\n";
        std::cout << "compression_ratio=" << compression_ratio << "\n";
        std::cout << "compressed_fraction=" << compressed_fraction << "\n";
        std::cout << "compression_time_us=" << compress_us << "\n";
        std::cout << "decompression_time_us=" << decompress_us << "\n";
        std::cout << "reconstruction_time_us=" << reconstruct_us << "\n";
        std::cout << "compressed_bytes=" << binary.size() << "\n";
        std::cout << "original_bytes=" << original_bytes << "\n";
        std::cout << "max_error=" << max_error << "\n";
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << "\n";
        return 1;
    }

    return 0;
}
