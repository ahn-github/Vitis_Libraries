/*
 * Copyright 2019 Xilinx, Inc.
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
 */
#include "table_dt.hpp"
#include "utils.hpp"
#include "cfg1.hpp"
#include "cfg2.hpp"
#include "tpch_read_2.hpp"

#include <sys/time.h>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <climits>
#include <unordered_map>
#include "gqe_api.hpp"
#include "q3.hpp"
int main(int argc, const char* argv[]) {
    std::cout << "\n------------ TPC-H GQE (1G) -------------\n";

    // cmd arg parser.
    ArgParser parser(argc, argv);

    std::string xclbin_path_h; // eg. q5kernel_VCU1525_hw.xclbin
    if (!parser.getCmdOption("-xclbin_h", xclbin_path_h)) {
        std::cout << "ERROR: xclbin path is not set!\n";
        return 1;
    }
    std::string xclbin_path_a; // eg. q5kernel_VCU1525_hw.xclbin
    if (!parser.getCmdOption("-xclbin_a", xclbin_path_a)) {
        std::cout << "ERROR: xclbin path is not set!\n";
        return 1;
    }

    std::string in_dir;
    if (!parser.getCmdOption("-in", in_dir) || !is_dir(in_dir)) {
        std::cout << "ERROR: input dir is not specified or not valid.\n";
        return 1;
    }

    int scale = 1;
    std::string scale_str;
    if (parser.getCmdOption("-c", scale_str)) {
        try {
            scale = std::stoi(scale_str);
        } catch (...) {
            scale = 1;
        }
    }

    int mini = 0;
    std::string mini_str;
    if (parser.getCmdOption("-mini", mini_str)) {
        try {
            mini = std::stoi(mini_str);
        } catch (...) {
            mini = 0;
        }
    }

    int32_t lineitem_n;
    int32_t orders_n;
    int32_t customer_n;

    if (mini) {
        lineitem_n = mini;
        orders_n = mini;
        customer_n = SF1_CUSTOMER;
    } else if (scale == 1) {
        lineitem_n = SF1_LINEITEM;
        orders_n = SF1_ORDERS;
        customer_n = SF1_CUSTOMER;
    } else if (scale == 30) {
        lineitem_n = SF30_LINEITEM;
        orders_n = SF30_ORDERS;
        customer_n = SF30_CUSTOMER;
    }
    std::cout << "NOTE:running in sf" << scale << " data\n.";

    int num_rep = 1;
    std::string num_str;
    if (parser.getCmdOption("-rep", num_str)) {
        try {
            num_rep = std::stoi(num_str);
        } catch (...) {
            num_rep = 1;
        }
    }
    if (num_rep > 20) {
        num_rep = 20;
        std::cout << "WARNING: limited repeat to " << num_rep << " times\n.";
    }

    // ********************************************************** //
    // Get CL devices.
    std::vector<cl::Device> devices = xcl::get_xil_devices();
    cl::Device device_h = devices[0];
    cl::Device device_a = devices[1];

    // Create context_h and command queue for selected device
    cl::Context context_h(device_h);
    cl::Context context_a(device_a);
    cl::CommandQueue q_h(context_h, device_h, CL_QUEUE_PROFILING_ENABLE | CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE);
    cl::CommandQueue q_a(context_a, device_a, CL_QUEUE_PROFILING_ENABLE | CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE);
    std::string devName_h = device_h.getInfo<CL_DEVICE_NAME>();
    std::string devName_a = device_a.getInfo<CL_DEVICE_NAME>();
    std::cout << "Selected Device_h " << devName_h << "\n";
    std::cout << "Selected Device_a " << devName_a << "\n";

    cl::Program::Binaries xclBins_h = xcl::import_binary_file(xclbin_path_h);
    cl::Program::Binaries xclBins_a = xcl::import_binary_file(xclbin_path_a);
    std::vector<cl::Device> devices_h;
    std::vector<cl::Device> devices_a;
    devices_h.push_back(device_h);
    devices_a.push_back(device_a);
    cl::Program program_h(context_h, devices_h, xclBins_h);
    cl::Program program_a(context_a, devices_a, xclBins_a);

    std::cout << "Kernel has been created\n";

    // ********************************************************* //

    /**
     * 1.Table and host cols Created
     */

    // for device table
    const int NumTable = 3;
    const int NumSweep = 1;
    const int NumSweep_h = 2;
    Table tbs[NumTable];
    tbs[0] = Table("lineitem", lineitem_n, 4, in_dir);
    tbs[0].addCol("l_orderkey", 4);
    tbs[0].addCol("l_extendedprice", 4);
    tbs[0].addCol("l_discount", 4);
    tbs[0].addCol("l_shipdate", 4);

    tbs[1] = Table("orders", orders_n, 4, in_dir);
    tbs[1].addCol("o_orderkey", 4);
    tbs[1].addCol("o_custkey", 4);
    tbs[1].addCol("o_orderdate", 4);
    tbs[1].addCol("o_shippriority", 4);

    tbs[2] = Table("customer", customer_n, 2, in_dir);
    tbs[2].addCol("c_custkey", 4);
    tbs[2].addCol("c_mktsegment", TPCH_READ_MAXAGG_LEN + 1);

    std::cout << "DEBUG0" << std::endl;

    // tbx is for the empty bufferB in kernel
    Table tbx(512);
    Table th0("th0", 180000 * scale, 8, "");
    Table th1("th1", 180000 * scale, 8, "");
    Table tk0("tk0", 180000 * scale, 8, "");
    Table tk1("tk1", 40000 * scale, 8, "");
    Table tk2("tk2", 20000 * scale, 16, "");

    Table th0_h("th0", 31000 * scale, 1, "");
    Table tk0_h("tk0", 180000 * scale, 8, "");
    Table tk1_h("tk1", 31000 * scale, 4, "");
    std::cout << "Table Creation done." << std::endl;

    /**
     * 2.allocate CPU
     */
    for (int i = 0; i < NumTable; i++) {
        tbs[i].allocateHost();
    }
    th0.allocateHost();
    th1.allocateHost();
    tk0.allocateHost();
    tk1.allocateHost();
    tk2.allocateHost();
    th0_h.allocateHost();
    tk0_h.allocateHost();
    tk1_h.allocateHost();
    // tk1.data = tk1_h.data;
    tk1 = tk1_h;

    std::cout << "Table allocation CPU done." << std::endl;

    /**
     * 3. load kernel config from dat and table from disk
     */
    AggrCfgCmd cfgcmds[NumSweep];
    AggrCfgCmd cfgcmd_out[NumSweep];

    for (int i = 0; i < NumSweep; i++) {
        cfgcmds[i].allocateHost();
        cfgcmd_out[i].allocateHost();

        get_aggr_cfg(cfgcmds[i].cmd, i);
    };

    cfgCmd h_cfgcmds[NumSweep_h];
    for (int i = 0; i < NumSweep_h; i++) {
        h_cfgcmds[i].allocateHost();
        //    get_cfg_dat(cfgcmds[i].cmd,"./host/q03/join/hexBin.dat",i);
    };
    get_cfg_dat_1(h_cfgcmds[0].cmd);
    get_cfg_dat_2(h_cfgcmds[1].cmd);

    for (int i = 0; i < NumTable; i++) {
        tbs[i].loadHost();
    };
    /**
     * 4.allocate device
     */
    tbs[0].allocateDevBuffer(context_h, 32);
    tbs[1].allocateDevBuffer(context_h, 32);
    tbs[2].allocateDevBuffer(context_h, 32);
    tk0.allocateDevBuffer(context_a, 33);
    tk2.allocateDevBuffer(context_a, 33);
    tk1.allocateDevBuffer(context_a, 32);

    th0.allocateDevBuffer(context_a, 33);
    th1.allocateDevBuffer(context_a, 33);

    tk0_h.allocateDevBuffer(context_h, 32);
    tk1_h.allocateDevBuffer(context_h, 32);
    th0_h.allocateDevBuffer(context_h, 32);

    for (int i = 0; i < NumSweep; i++) {
        cfgcmds[i].allocateDevBuffer(context_a, 32);
        cfgcmd_out[i].allocateDevBuffer(context_a, 33);
    };

    for (int i = 0; i < NumSweep_h; i++) {
        h_cfgcmds[i].allocateDevBuffer(context_h, 32);
    };

    std::cout << "Table allocation device done." << std::endl;

    /**
     * 5.kernels (host and device)
     */

    int kernelInd = 0;
    AggrBufferTmp buftmp(context_a);
    buftmp.BufferInitial(q_a);
    // kernel Engine
    AggrKrnlEngine krnlstep[NumSweep];
    for (int i = 0; i < NumSweep; i++) {
        krnlstep[i] = AggrKrnlEngine(program_a, q_a, "gqeAggr");
    }
    krnlstep[0].setup(tk1, tk2, cfgcmds[0], cfgcmd_out[0], buftmp);

    bufferTmp h_buftmp(context_h);
    h_buftmp.initBuffer(q_h);
    // kernel Engine
    krnlEngine h_krnlstep[NumSweep_h];
    for (int i = 0; i < NumSweep_h; i++) {
        h_krnlstep[i] = krnlEngine(program_h, q_h, "gqeJoin");
    }

    h_krnlstep[0].setup(th0_h, tbs[1], tk0_h, h_cfgcmds[0], h_buftmp);
    h_krnlstep[1].setup(tk0_h, tbs[0], tk1_h, h_cfgcmds[1], h_buftmp);
    // transfer Engine
    transEngine transin[NumSweep];
    transEngine transout[NumSweep];
    for (int i = 0; i < NumSweep; i++) {
        transin[i].setq(q_a);
        transout[i].setq(q_a);
    }

    for (int i = 0; i < NumSweep; i++) {
        transin[0].add(&(cfgcmds[i]));
    };
    q_a.finish();

    transEngine h_transin[NumSweep_h + 1];
    transEngine h_transout[NumSweep_h];
    for (int i = 0; i < NumSweep_h + 1; i++) {
        h_transin[i].setq(q_h);
        h_transout[i].setq(q_h);
    }

    h_transin[0].add(&(tbs[1]));

    for (int i = 0; i < NumSweep_h; i++) {
        h_transin[0].add(&(h_cfgcmds[i]));
    };
    q_h.finish();
    std::cout << "Kernel/Transfer have been setup." << std::endl;

    // events
    std::vector<cl::Event> eventsh2d_write[NumSweep];
    std::vector<cl::Event> eventsd2h_read[NumSweep];
    std::vector<cl::Event> events[NumSweep];
    for (int i = 0; i < NumSweep; i++) {
        events[i].resize(1);
    };
    for (int i = 0; i < NumSweep; i++) {
        eventsh2d_write[i].resize(1);
    };
    for (int i = 0; i < NumSweep; i++) {
        eventsd2h_read[i].resize(1);
    };

    std::vector<cl::Event> h_eventsh2d_write[NumSweep_h + 1];
    std::vector<cl::Event> h_eventsd2h_read[NumSweep_h];
    std::vector<cl::Event> h_events[NumSweep_h];
    std::vector<cl::Event> h_events_grp[NumSweep_h];
    for (int i = 0; i < NumSweep_h; i++) {
        h_events[i].resize(1);
    };
    for (int i = 0; i < NumSweep_h + 1; i++) {
        h_eventsh2d_write[i].resize(1);
    };
    for (int i = 0; i < NumSweep_h; i++) {
        h_eventsd2h_read[i].resize(1);
    };

    struct timeval tv[8];
#ifdef INI
    tk0_h.initBuffer(q_h);
    tk1_h.initBuffer(q_h);
    tk2.initBuffer(q_a);
#endif
    gettimeofday(&tv[0], 0);
    // step1 :filter of customer
    h_transin[0].host2dev(0, nullptr, &(h_eventsh2d_write[0][0]));

    // step3 : kernel-join
    gettimeofday(&tv[1], 0);
    q3FilterC(tbs[2], th0_h);
    gettimeofday(&tv[2], 0);

    h_transin[1].add(&th0_h);
    h_transin[1].host2dev(0, &(h_eventsh2d_write[0]), &(h_eventsh2d_write[1][0]));
    h_krnlstep[0].run(0, &(h_eventsh2d_write[1]), &(h_events[0][0]));
    kernelInd++;

    // step4 : kernel-join
    //  q3Join_C_O2(tk0,tbs[0],tk1);
    h_transin[2].add(&(tbs[0]));
    h_transin[2].host2dev(0, &(h_eventsh2d_write[1]), &(h_eventsh2d_write[2][0]));
    h_events_grp[0].push_back(h_eventsh2d_write[2][0]);
    h_events_grp[0].push_back(h_events[0][0]);
    h_krnlstep[1].run(0, &(h_events_grp[0]), &(h_events[1][0]));

    // step5 : D2H
    h_transout[kernelInd].add(&tk1_h);
    h_transout[kernelInd].dev2host(0, &(h_events[1]), &(h_eventsd2h_read[kernelInd][0]));
    q_h.finish();

    //  memcpy(tk1.data,tk1_h.data,4*64*sizeonecol);
    gettimeofday(&tv[3], 0);

    transin[0].add(&tk1);
    transin[0].host2dev(0, nullptr, &(eventsh2d_write[0][0]));

    // step5 : kernel-aggr
    krnlstep[0].run(0, &(eventsh2d_write[0]), &(events[0][0]));

    // step6 : D2H
    transout[0].add(&tk2);
    transout[0].dev2host(0, &(events[0]), &(eventsd2h_read[0][0]));
    q_a.finish();

    gettimeofday(&tv[4], 0);

    q3Sort(tk2, tk1);
    gettimeofday(&tv[5], 0);

    cl_ulong kstart;
    h_eventsh2d_write[0][0].getProfilingInfo(CL_PROFILING_COMMAND_START, &kstart);
    print_d_time(h_eventsh2d_write[0][0], h_eventsh2d_write[0][0], kstart, "xclbin_h trans 0");
    print_h_time(tv[0], tv[1], tv[2], "FilterC..");
    print_h_time(tv[0], tv[2], tv[3], "Kernel xcbin_h");
    print_h_time(tv[0], tv[3], tv[4], "Kernel xcbin_a");
    print_h_time(tv[0], tv[4], tv[5], "Sort");
    std::cout << "All execution time of Host " << tvdiff(&tv[0], &tv[5]) / 1000 << " ms" << std::endl;

    q3Print(tk1);

    return 1;
}
