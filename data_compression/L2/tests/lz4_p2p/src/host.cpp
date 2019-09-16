/*
 * (c) Copyright 2019 Xilinx, Inc. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */
#include "xil_lz4.hpp"
#include <fstream>
#include <vector>
#include "cmdlineparser.h"

void xilCompressTop(std::string& compress_mod,
                      uint32_t block_size,
                      std::string& compress_bin,
                      std::string& single_bin,
                      int enable_p2p,
                      uint8_t device_id) {
    // Xilinx LZ4 object
    xil_lz4 xlz;

    // LZ4 Compression Binary Name
    std::string binaryFileName;
    if (SINGLE_XCLBIN)
        binaryFileName = single_bin;
    else
        binaryFileName = compress_bin;
    xlz.m_bin_flow = 1;

    // Create xil_lz4 object
    xlz.init(binaryFileName, device_id);
#ifdef VERBOSE
    std::cout << "\n";
    std::cout << "E2E(MBps)\tKT(MBps)\tLZ4_CR\t\tFile Size(MB)\t\tFile Name" << std::endl;
    std::cout << "\n";
#endif
    std::ifstream inFile(compress_mod.c_str(), std::ifstream::binary);
    if (!inFile) {
        std::cout << "Unable to open file";
        exit(1);
    }
    uint32_t input_size = get_file_size(inFile);
    inFile.close();
#ifdef VERBOSE
    std::cout << "input_size = " << input_size << " bytes." << std::endl;
#endif

    std::string lz_compress_in = compress_mod;
    std::string lz_compress_out = compress_mod;
    lz_compress_out = lz_compress_out + ".lz4";

    // Update class membery with block_size
    xlz.m_block_size_in_kb = block_size;

    // 0 means Xilinx flow
    xlz.m_switch_flow = 0;

    // Call LZ4 compression
    auto total_start = std::chrono::high_resolution_clock::now();
    uint32_t enbytes = xlz.compress_file(lz_compress_in, lz_compress_out, enable_p2p);
    auto total_end = std::chrono::high_resolution_clock::now();
    auto total_time_ns = std::chrono::duration<double, std::nano>(total_end - total_start);

#ifdef VERBOSE
    std::cout.precision(3);
    std::cout << "\t\t" << (float)input_size / enbytes << "\t\t" << (float)input_size / 1000000 << "\t\t\t"
              << lz_compress_in << std::endl;
    std::cout << "\n";
    std::cout << "Output Location: " << lz_compress_out.c_str() << std::endl;
    std::cout << "Compressed file size: " << enbytes << std::endl;
    /* total time taken for read from disk + compress & packer kernels + write to disk operations */
    std::cout << "Total Time (milli sec): " << total_time_ns.count() / 1000000 << std::endl;
#endif

    xlz.release();
}

void xilValidate(std::string& file_list, std::string& ext) {
    std::cout << "\n";
    std::cout << "Status\t\tFile Name" << std::endl;
    std::cout << "\n";

    std::ifstream infilelist_val(file_list.c_str());
    std::string line_val;

    while (std::getline(infilelist_val, line_val)) {
        std::string line_in = line_val;
        std::string line_out = line_in + ext;

        int ret = 0;
        // Validate input and output files
        ret = validate(line_in, line_out);
        if (ret == 0) {
            std::cout << (ret ? "FAILED\t" : "PASSED\t") << "\t" << line_in << std::endl;
        } else {
            std::cout << "Validation Failed" << line_out.c_str() << std::endl;
            //        exit(1);
        }
    }
}

void xilCompressDecompressList(std::string& file_list,
                                  std::string& ext1,
                                  std::string& ext2,
                                  bool c_flow,
                                  bool d_flow,
                                  uint32_t block_size,
                                  std::string& compress_bin,
                                  std::string& decompress_bin,
                                  std::string& single_bin) {
    // Compression
    // LZ4 Compression Binary Name
    std::string binaryFileName_compress;
    if (SINGLE_XCLBIN)
        binaryFileName_compress = single_bin;
    else
        binaryFileName_compress = compress_bin;

    // Create xil_lz4 object
    xil_lz4 xlz;

    if (c_flow == 0) {
        std::cout << "\n";
        xlz.m_bin_flow = 1;
        xlz.init(binaryFileName_compress, 0);
    }
    std::cout << "\n";

    std::cout << "--------------------------------------------------------------" << std::endl;
    if (c_flow == 0)
        std::cout << "                     Xilinx Compress                          " << std::endl;
    else
        std::cout << "                     Standard Compress                        " << std::endl;
    std::cout << "--------------------------------------------------------------" << std::endl;

    if (c_flow == 0) {
        std::cout << "\n";
        std::cout << "COMP(MBps)\tPACK(MBps)\tLZ4_CR\t\tFile Size(MB)\t\tFile Name" << std::endl;
        std::cout << "\n";
    } else {
        std::cout << "\n";
        std::cout << "File Size(MB)\t\tFile Name" << std::endl;
        std::cout << "\n";
    }

    std::ifstream infilelist(file_list.c_str());
    std::string line;

    // Compress list of files
    // This loop does LZ4 compression on list
    // of files.
    while (std::getline(infilelist, line)) {
        std::ifstream inFile(line.c_str(), std::ifstream::binary);
        if (!inFile) {
            std::cout << "Enc Unable to open file";
            exit(1);
        }

        uint32_t input_size = get_file_size(inFile);
        inFile.close();

        std::string lz_compress_in = line;
        std::string lz_compress_out = line;
        lz_compress_out = lz_compress_out + ext1;

        xlz.m_block_size_in_kb = block_size;
        xlz.m_switch_flow = c_flow;

        // Call LZ4 compression
        uint32_t enbytes = xlz.compress_file(lz_compress_in, lz_compress_out, 0);
        if (c_flow == 0) {
            std::cout << "\t\t" << (float)input_size / enbytes << "\t\t" << (float)input_size / 1000000 << "\t\t\t"
                      << lz_compress_in << std::endl;
        } else {
            std::cout << std::fixed << std::setprecision(3);
            std::cout << (float)input_size / 1000000 << "\t\t\t" << lz_compress_in << std::endl;
        }
    }

    if (c_flow == 0) {
        if (!SINGLE_XCLBIN) {
            xlz.release();
            //std::cout << "Device released ..." << std::endl;
        }
    }

    // De-Compression
    // LZ4 Decompression Binary Name
    std::string binaryFileName_decompress;
    if (!SINGLE_XCLBIN) binaryFileName_decompress = decompress_bin;

    // Xilinx LZ4 object
    if (d_flow == 0) {
        // Create xil_lz4 object
        std::cout << "\n";
        xlz.m_bin_flow = 0;
        if (!SINGLE_XCLBIN) {
            xlz.init(binaryFileName_decompress, 0);
        }
        if (SINGLE_XCLBIN && c_flow == 1) {
            xlz.init(binaryFileName_compress, 0);
        }
    }

    std::ifstream infilelist_dec(file_list.c_str());
    std::string line_dec;

    std::cout << "\n";
    std::cout << "--------------------------------------------------------------" << std::endl;
    if (d_flow == 0)
        std::cout << "                     Xilinx De-Compress                       " << std::endl;
    else
        std::cout << "                     Standard De-Compress                     " << std::endl;

    std::cout << "--------------------------------------------------------------" << std::endl;
    if (d_flow == 0) {
        std::cout << "\n";
        std::cout << "E2E(MBps)\tKT(MBps)\tFile Size(MB)\t\tFile Name" << std::endl;
        std::cout << "\n";
    } else {
        std::cout << "\n";
        std::cout << "File Size(MB)\tFile Name" << std::endl;
        std::cout << "\n";
    }

    // Decompress list of files
    // This loop does LZ4 decompress on list
    // of files.
    while (std::getline(infilelist_dec, line_dec)) {
        std::string file_line = line_dec;
        file_line = file_line + ext2;

        std::ifstream inFile_dec(file_line.c_str(), std::ifstream::binary);
        if (!inFile_dec) {
            std::cout << "Dec Unable to open file";
            exit(1);
        }

        uint64_t input_size = get_file_size(inFile_dec);
        inFile_dec.close();

        std::string lz_decompress_in = file_line;
        std::string lz_decompress_out = file_line;
        lz_decompress_out = lz_decompress_out + ".orig";

        // Call LZ4 decompression
        xlz.m_switch_flow = d_flow;
        xlz.decompress_file(lz_decompress_in, lz_decompress_out);

        if (d_flow == 0) {
            std::cout << "\t\t" << (float)input_size / 1000000 << "\t\t" << lz_decompress_in << std::endl;
        } else {
            std::cout << std::fixed << std::setprecision(3);
            std::cout << (float)input_size / 1000000 << "\t\t" << lz_decompress_in << std::endl;
        }
    } // While loop ends

    if (d_flow == 0) {
        xlz.release();
    }
}
void xilBatchVerify(std::string& file_list,
                      int f,
                      uint32_t block_size,
                      std::string& compress_bin,
                      std::string& decompress_bin,
                      std::string& single_bin) {
    if (f == 0) { // All flows are tested (Xilinx, Standard)

        // Xilinx LZ4 flow

        // Flow : Xilinx LZ4 Compress vs Xilinx LZ4 Decompress
        {
            // Xilinx LZ4 compression
            std::string ext1 = ".xe2xd.lz4";
            std::string ext2 = ".xe2xd.lz4";
            xilCompressDecompressList(file_list, ext1, ext2, 0, 0, block_size, compress_bin, decompress_bin,
                                         single_bin);

            // Validate
            std::cout << "\n";
            std::cout << "----------------------------------------------------------------------------------------"
                      << std::endl;
            std::cout << "                       Validate: Xilinx LZ4 Compress vs Xilinx LZ4 Decompress           "
                      << std::endl;
            std::cout << "----------------------------------------------------------------------------------------"
                      << std::endl;
            std::string ext3 = ".xe2xd.lz4.orig";
            xilValidate(file_list, ext3);
        }

        // Standard LZ4 flow

        // Flow : Xilinx LZ4 Compress vs Standard LZ4 Decompress
        {
            // Xilinx LZ4 compression
            std::string ext2 = ".xe2sd.lz4";
            std::string ext1 = ".xe2sd.lz4";
            xilCompressDecompressList(file_list, ext1, ext2, 0, 1, block_size, compress_bin, decompress_bin,
                                         single_bin);

            std::cout << "\n";
            std::cout << "---------------------------------------------------------------------------------------"
                      << std::endl;
            std::cout << "                       Validate: Xilinx LZ4 Compress vs Standard LZ4 Decompress        "
                      << std::endl;
            std::cout << "---------------------------------------------------------------------------------------"
                      << std::endl;
            std::string ext3 = ".xe2sd";
            xilValidate(file_list, ext3);

        } // End of Flow : Xilinx LZ4 Compress vs Standard LZ4 Decompress

        { // Start of Flow : Standard LZ4 Compress vs Xilinx LZ4 Decompress

            // Standard LZ4 compression
            std::string ext1 = ".se2xd";
            std::string ext2 = ".std.lz4";
            xilCompressDecompressList(file_list, ext1, ext2, 1, 0, block_size, compress_bin, decompress_bin,
                                         single_bin);

            // Validate
            std::cout << "\n";
            std::cout << "----------------------------------------------------------------------------------------"
                      << std::endl;
            std::cout << "                       Validate: Standard Compress vs Xilinx LZ4 Decompress             "
                      << std::endl;
            std::cout << "----------------------------------------------------------------------------------------"
                      << std::endl;
            std::string ext = ".std.lz4.orig";
            xilValidate(file_list, ext);

        } // End of Flow : Standard LZ4 Compress vs Xilinx LZ4 Decompress

    }                  // Flow = 0 ends here
    else if (f == 1) { // Only Xilinx flows are tested

        // Flow : Xilinx LZ4 Compress vs Xilinx LZ4 Decompress
        {
            // Xilinx LZ4 compression
            std::string ext1 = ".xe2xd.lz4";
            std::string ext2 = ".xe2xd.lz4";
            xilCompressDecompressList(file_list, ext1, ext2, 0, 0, block_size, compress_bin, decompress_bin,
                                         single_bin);

            // Validate
            std::cout << "\n";
            std::cout << "----------------------------------------------------------------------------------------"
                      << std::endl;
            std::cout << "                       Validate: Xilinx LZ4 Compress vs Xilinx LZ4 Decompress           "
                      << std::endl;
            std::cout << "----------------------------------------------------------------------------------------"
                      << std::endl;
            std::string ext3 = ".xe2xd.lz4.orig";
            xilValidate(file_list, ext3);
        }

    } // Flow = 1 ends here
    else if (f == 2) {
        // Flow : Xilinx LZ4 Compress vs Standard LZ4 Decompress
        {
            // Xilinx LZ4 compression
            std::string ext1 = ".xe2sd.lz4";
            std::string ext2 = ".xe2sd.lz4";
            xilCompressDecompressList(file_list, ext1, ext2, 0, 1, block_size, compress_bin, decompress_bin,
                                         single_bin);

            // Validate
            std::cout << "\n";
            std::cout << "---------------------------------------------------------------------------------------"
                      << std::endl;
            std::cout << "                       Validate: Xilinx LZ4 Compress vs Standard LZ4 Decompress        "
                      << std::endl;
            std::cout << "---------------------------------------------------------------------------------------"
                      << std::endl;
            std::string ext3 = ".xe2sd";
            xilValidate(file_list, ext3);

        } // End of Flow : Xilinx LZ4 Compress vs Standard LZ4 Decompress

    } // Flow = 2 ends here
    else if (f == 3) {
        { // Start of Flow : Standard LZ4 Compress vs Xilinx LZ4 Decompress

            // Standard LZ4 compression
            std::string ext1 = ".se2xd";
            std::string ext2 = ".std.lz4";
            xilCompressDecompressList(file_list, ext1, ext2, 1, 0, block_size, compress_bin, decompress_bin,
                                         single_bin);

            // Validate
            std::cout << "\n";
            std::cout << "----------------------------------------------------------------------------------------"
                      << std::endl;
            std::cout << "                       Validate: Standard Compress vs Xilinx LZ4 Decompress             "
                      << std::endl;
            std::cout << "----------------------------------------------------------------------------------------"
                      << std::endl;
            std::string ext = ".std.lz4.orig";
            xilValidate(file_list, ext);

        } // End of Flow : Standard LZ4 Compress vs Xilinx LZ4 Decompress
    }     // Flow = 3 ends here
    else {
        std::cout << "-x option is wrong" << f << std::endl;
        std::cout << "-x - 0 all features" << std::endl;
        std::cout << "-x - 1 Xilinx (C/D)" << std::endl;
        std::cout << "-x - 2 Xilinx Compress vs Standard Decompress" << std::endl;
        std::cout << "-x - 3 Standard Compress vs Xilinx Decompress" << std::endl;
    }
}

void xilDecompressTop(std::string& decompress_mod, std::string& decompress_bin, std::string& single_bin) {
    // Create xil_lz4 object
    xil_lz4 xlz;

    // LZ4 Decompression Binary Name
    std::string binaryFileName_decompress;
    if (SINGLE_XCLBIN)
        binaryFileName_decompress = single_bin;
    else
        binaryFileName_decompress = decompress_bin;
    xlz.m_bin_flow = 0;
    xlz.init(binaryFileName_decompress, 0);

#ifdef VERBOSE
    std::cout << "\n";
    std::cout << "E2E(MBps)\tKT(MBps)\tFile Size(MB)\t\tFile Name" << std::endl;
    std::cout << "\n";
#endif

    std::ifstream inFile(decompress_mod.c_str(), std::ifstream::binary);
    if (!inFile) {
        std::cout << "Unable to open file";
        exit(1);
    }

    uint32_t input_size = get_file_size(inFile);
    inFile.close();

    string lz_decompress_in = decompress_mod;
    string lz_decompress_out = decompress_mod;
    lz_decompress_out = lz_decompress_out + ".orig";

    xlz.m_switch_flow = 0;

    // Call LZ4 decompression
    xlz.decompress_file(lz_decompress_in, lz_decompress_out);
#ifdef VERBOSE
    std::cout << "\t\t" << (float)input_size / 1000000 << "\t\t\t" << lz_decompress_in << std::endl;
    std::cout << "\n";
    std::cout << "Output Location: " << lz_decompress_out.c_str() << std::endl;
#endif
    xlz.release();
}

int main(int argc, char* argv[]) {
    sda::utils::CmdLineParser parser;
    parser.addSwitch("--compress_xclbin", "-cx", "Compress XCLBIN", "compress");
    parser.addSwitch("--decompress_xclbin", "-dx", "DeCompress XCLBIN", "decompress");
    parser.addSwitch("--single_xclbin", "-sx", "Single XCLBIN", "compress_decompress");
    parser.addSwitch("--compress", "-c", "Compress", "");
    parser.addSwitch("--file_list", "-l", "List of Input Files", "");
    parser.addSwitch("--decompress", "-d", "Decompress", "");
    parser.addSwitch("--block_size", "-B", "Compress Block Size [0-64: 1-256: 2-1024: 3-4096]", "0");
    parser.addSwitch("--flow", "-x", "Validation [0-All: 1-XcXd: 2-XcSd: 3-ScXd]", "1");
    parser.addSwitch("--p2p", "-p2p", "P2P enable", "0");
    parser.addSwitch("--id", "-id", "Device ID", "0");
    parser.parse(argc, argv);

    std::string compress_bin = parser.value("compress_xclbin");
    std::string decompress_bin = parser.value("decompress_xclbin");
    std::string single_bin = parser.value("single_xclbin");
    std::string compress_mod = parser.value("compress");
    std::string filelist = parser.value("file_list");
    std::string decompress_mod = parser.value("decompress");
    std::string flow = parser.value("flow");
    std::string block_size = parser.value("block_size");
    std::string p2p = parser.value("p2p");
    std::string device_ids = parser.value("id");

    int enable_p2p = 0;
    uint8_t device_id = 0;

    if (!(p2p.empty())) {
        enable_p2p = atoi(p2p.c_str());
    }

    if (!(device_ids.empty())) {
        device_id = atoi(device_ids.c_str());
    }
    uint32_t bSize = 0;
    // Block Size
    if (!(block_size.empty())) {
        bSize = atoi(block_size.c_str());

        switch (bSize) {
            case 0:
                bSize = 64;
                break;
            case 1:
                bSize = 256;
                break;
            case 2:
                bSize = 1024;
                break;
            case 3:
                bSize = 4096;
                break;
            default:
                std::cout << "Invalid Block Size provided" << std::endl;
                parser.printHelp();
                exit(1);
        }
    } else {
        // Default Block Size - 64KB
        bSize = BLOCK_SIZE_IN_KB;
    }

    int fopt = 0;
    if (!(flow.empty()))
        fopt = atoi(flow.c_str());
    else
        fopt = 1;

    // "-c" Compress Mode
    if (!compress_mod.empty()) xilCompressTop(compress_mod, bSize, compress_bin, single_bin, enable_p2p, device_id);

    // "-d" Decompress Mode
    if (!decompress_mod.empty()) xilDecompressTop(decompress_mod, decompress_bin, single_bin);

    // "-l" List of Files
    if (!filelist.empty()) {
        if (fopt == 0 || fopt == 2 || fopt == 3) {
            std::cout << "\n" << std::endl;
            std::cout << "Validation flows with Standard LZ4 ";
            std::cout << "requires executable" << std::endl;
            std::cout << "Please build LZ4 executable ";
            std::cout << "from following source ";
            std::cout << "https://github.com/lz4/lz4.git" << std::endl;
        }

        xilBatchVerify(filelist, fopt, bSize, compress_bin, decompress_bin, single_bin);
    }
}