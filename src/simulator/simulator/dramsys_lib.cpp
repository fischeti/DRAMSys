#include "Simulator.h"

#include <DRAMSys/config/DRAMSysConfiguration.h>
#include <filesystem>
#include "elfloader.h"

#define svOpenArrayHandle void*

std::vector<uint64_t>                               list_of_DRAMburst;
std::vector<uint64_t>                               list_of_DRAMsize;
std::vector<uint64_t>                               list_of_DRAMBase;
std::vector<DRAMSys::DRAMSys *>                     list_of_DRAMsys;
std::vector<dramsys_conv *>                         list_of_conv;
std::vector<uint8_t *>                              list_of_wbuffer;
std::vector<uint8_t *>                              list_of_wstrobe;

extern "C" int add_dram(char * resources_path, char * simulationJson_path, uint64_t dram_base){

    static int id = 0;

    std::filesystem::path resourceDirectory = DRAMSYS_RESOURCE_DIR;
    if (resources_path != 0)
    {
        resourceDirectory = resources_path;
    }

    std::filesystem::path baseConfig = resourceDirectory / "hbm2-example.json";
    if (simulationJson_path != 0)
    {
        baseConfig = simulationJson_path;
    }

    DRAMSys::Config::Configuration configuration =
        DRAMSys::Config::from_path(baseConfig.c_str(), resourceDirectory.c_str());

    DRAMSys::DRAMSys * dramSys;
    dramsys_conv * conv;

    std::string dramsys_name = "DRAMSysRecordable";
    std::string conv_name = "dramsys_conv";
    std::string id_str = std::to_string(id);
    dramsys_name = dramsys_name + id_str;
    conv_name = conv_name + id_str;

    if (configuration.simconfig.DatabaseRecording.value_or(false))
    {
        dramSys = new DRAMSys::DRAMSysRecordable(dramsys_name.c_str(), configuration);
    }
    else
    {
        dramSys = new DRAMSys::DRAMSys(dramsys_name.c_str(), configuration);
    }

    conv = new dramsys_conv(conv_name.c_str());

    conv->iSocket.bind(dramSys->tSocket);

    //init systemC engine
    if (id == 0)
    {
        auto start = std::chrono::high_resolution_clock::now();
        sc_set_stop_mode(SC_STOP_FINISH_DELTA);
    }

    DRAMSys::Configuration config;
    config.loadMemSpec(configuration.memspec);
    uint64_t dramChannelSize = config.memSpec->getSimMemSizeInBytes() / config.memSpec->numberOfChannels;
    uint64_t dramMaxBurstByte = config.memSpec->maxBytesPerBurst;

    //put them into vector container
    list_of_DRAMsys.push_back(dramSys);
    list_of_conv.push_back(conv);
    list_of_DRAMsize.push_back(dramChannelSize);
    list_of_DRAMBase.push_back(dram_base);
    list_of_DRAMburst.push_back(dramMaxBurstByte);

    uint8_t* wbuffer_ptr = new uint8_t [2048];
    list_of_wbuffer.push_back(wbuffer_ptr);

    uint8_t* wstrobe_ptr = new uint8_t [2048];
    list_of_wstrobe.push_back(wstrobe_ptr);


    std::cout << "the instantiated DRAM id is: " << id << std::endl;

    int res = id;
    id += 1;
    return res;

}



extern "C" int dram_can_accept_req(int dram_id) {

    // std::cout << "dram_can_accept_req:  #" << dram_id << std::endl;
    return list_of_conv[dram_id]->dram_can_accept_req();
}


extern "C" int dram_has_read_rsp(int dram_id) {

    // std::cout << "dram_has_read_rsp:  #" << dram_id << std::endl;
    return list_of_conv[dram_id]->dram_has_read_rsp();
}

extern "C" int dram_has_write_rsp(int dram_id) {

    // std::cout << "dram_has_read_rsp:  #" << dram_id << std::endl;
    return list_of_conv[dram_id]->dram_has_write_rsp();
}

extern "C" int dram_get_write_rsp(int dram_id) {
    return list_of_conv[dram_id]->dram_get_write_rsp();
}

extern "C" void dram_write_buffer(int dram_id, int byte_int, int idx) {

    // std::cout << "dram_send_req:  #" << dram_id << std::endl;
    ((uint8_t *)(list_of_wbuffer[dram_id]))[idx] = (uint8_t)byte_int;
}

extern "C" void dram_write_strobe(int dram_id, int strob_int, int idx) {

    // std::cout << "dram_send_req:  #" << dram_id << std::endl;
    ((uint8_t *)(list_of_wstrobe[dram_id]))[idx] = strob_int != 0? TLM_BYTE_ENABLED: TLM_BYTE_DISABLED;
}

extern "C" void dram_send_req(int dram_id, uint64_t addr, uint64_t length , uint64_t is_write, uint64_t strob_enable) {

    // std::cout << "dram_send_req:  #" << dram_id << std::endl;
    if (is_write && strob_enable && (length > list_of_DRAMburst[dram_id]))
    {
        if (length%list_of_DRAMburst[dram_id] != 0) SC_REPORT_FATAL("dramsys_conv", "cannot tackle strob write with misaligned size");
        int num_subreq = length/list_of_DRAMburst[dram_id];
        uint64_t sub_addr = addr;
        uint8_t * wbuf_ptr = list_of_wbuffer[dram_id];
        uint8_t * wstrb_ptr = list_of_wstrobe[dram_id];
        for (int i = 0; i < num_subreq; ++i)
        {
            list_of_conv[dram_id]->dram_send_req(sub_addr, list_of_DRAMburst[dram_id], is_write, strob_enable, wbuf_ptr, wstrb_ptr);
            sub_addr = sub_addr + list_of_DRAMburst[dram_id];
            wbuf_ptr = wbuf_ptr + list_of_DRAMburst[dram_id];
            wstrb_ptr = wstrb_ptr + list_of_DRAMburst[dram_id];
        }

    } else {
        list_of_conv[dram_id]->dram_send_req(addr, length, is_write, strob_enable, list_of_wbuffer[dram_id], list_of_wstrobe[dram_id]);
    }
}

extern "C" void dram_get_read_rsp(int dram_id, uint64_t length, const svOpenArrayHandle buf) {

    // std::cout << "dram_get_read_rsp:  #" << dram_id << std::endl;
    list_of_conv[dram_id]->dram_get_read_rsp(length, (uint8_t *)buf);
    // std::cout << "p10"<< std::endl;
}

extern "C" int dram_get_read_rsp_byte(int dram_id) {

    uint8_t byte;
    int byte_int;
    byte = list_of_conv[dram_id]->dram_get_read_rsp_byte();
    byte_int = (int)byte;
    // std::cout << "dram_get_read_rsp_byte: " << byte_int << std::endl;
    return byte_int;
}

extern "C" int dram_peek_read_rsp_byte(int dram_id, int idx) {
    return (int)list_of_conv[dram_id]->dram_peek_read_rsp_byte(idx);
}

extern "C" int dram_pop_read_rsp_byte(int dram_id) {
    return (int)list_of_conv[dram_id]->dram_pop_read_rsp_byte();
}

extern "C" void run_ns(int ns) {
    sc_start(ns, SC_NS);
}

extern "C" int dram_get_inflight_read(int dram_id) {
    return list_of_conv[dram_id]->inflight_read_cnt;
}


extern "C" void close_dram(int dram_id) {
    if(dram_id == 0) sc_stop();
    delete list_of_conv[dram_id];
    delete list_of_DRAMsys[dram_id];
}


extern "C" void dram_preload_byte(int dram_id, uint64_t dram_addr_ofst, int byte_int) {
    list_of_DRAMsys[dram_id]->preloadByte(dram_addr_ofst,byte_int);
}

extern "C" int dram_check_byte(int dram_id, uint64_t dram_addr_ofst) {
    return list_of_DRAMsys[dram_id]->checkByte(dram_addr_ofst);
}

extern "C" int check_symbol(int dram_id, const char* sym) {
    uint64_t symbol_ptr = elf_get_symbol_addr(sym);
    int value = 0;
    uint64_t dram_addr_offset = list_of_DRAMBase[dram_id];
    for (int i = 0; i < 4; i++)
    {
        value |= list_of_DRAMsys[dram_id]->checkByte(symbol_ptr + i - dram_addr_offset) << (i*8);
    }
    return value;
}

extern "C" void write_symbol(int dram_id, const char* sym, int value) {
    uint64_t symbol_ptr = elf_get_symbol_addr(sym);
    uint64_t dram_addr_offset = list_of_DRAMBase[dram_id];
    for (int i = 0; i < 4; i++) {
        list_of_DRAMsys[dram_id]->preloadByte(symbol_ptr + i - dram_addr_offset, (value >> (i*8)) & 0xFF);
    }
}

extern "C" void dram_load_elf(int dram_id, uint64_t, char * elf_path) {
    std::string app_binary;
    app_binary = elf_path;
    std::ifstream f(app_binary.c_str());
    if (list_of_DRAMsize.size() <= dram_id)
    {
        std::cout << "DRAM id " << dram_id << " is not yet initialized" << std::endl;
        return;
    }
    if (f.good())
    {
        elfloader_read_elf(app_binary.c_str(), list_of_DRAMsize[dram_id], list_of_DRAMBase[dram_id], list_of_DRAMsys[dram_id]->getDramBasePointer());
        std::cout << "Load elf file [" << app_binary << "] in DRAM id " << dram_id << std::endl;
    } else {
        std::cout << "Can not Load elf file [" << app_binary << "] in DRAM id " << dram_id << " : File not found"<< std::endl;
    }
}

extern "C" void dram_load_memfile(int dram_id, uint64_t addr_ofst, char * mem_path){

    //Memory pre-loading
    std::string read_byte;
    uint64_t addr = addr_ofst;
    std::cout << "Load mem file [" << mem_path << "] in DRAM id " << dram_id << " from addr " << addr_ofst << std::endl;
    std::ifstream MemFile(mem_path);
    //sanity check
    if (!MemFile.good())
    {
        std::cout << "Can not Load Mem file [" << mem_path << "] in DRAM id " << dram_id << " : File not found"<< std::endl;
        return;
    }

    //Get the total file size
    MemFile.seekg(0, std::ios::end);
    std::streampos fileSize = MemFile.tellg();
    MemFile.seekg(0, std::ios::beg);


    double percentage_cnt = 0.0;
    double cnt_add = 0.5;
    //Load Mem file
    while(getline(MemFile,read_byte)){
        //Calculate the percentage loaded
        std::streampos currentPosition = MemFile.tellg();
        double percentage = (static_cast<double>(currentPosition) / fileSize) * 100.0;

        //Write to Dram Buffer
        unsigned int data;
        std::stringstream ss;
        ss << std::hex << read_byte;
        ss >> data;
        list_of_DRAMsys[dram_id]->preloadByte(addr,data);
        addr++;
    }
    MemFile.close();
    std::cout << std::endl;
    std::cout << "Load Mem Completed !" << std::endl;
}

extern "C" void dram_register_async_callback(int dram_id, CallbackInstance_t instance, AsynCallbackResp_Meth* resp_meth, AsynCallbackUpdateReq_Meth* req_meth) {
    list_of_conv[dram_id]->registerCBInstance(instance);
    list_of_conv[dram_id]->registerCBRespMeth(resp_meth);
    list_of_conv[dram_id]->registerCBUpdateReqMeth(req_meth);
}
