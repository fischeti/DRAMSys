#pragma once

#include "simulator/MemoryManager.h"

#include <systemc>
#include <iostream>
#include <fstream>
#include <deque>
#include <string>
#include <math.h>
#include <queue>
#include <list>
#include <memory>
#include <fcntl.h>
#include <unistd.h>

#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <list>
#include <chrono>

#include <systemc>
#include <tlm>
#include <tlm_utils/peq_with_cb_and_phase.h>
#include <tlm_utils/simple_initiator_socket.h>

using namespace sc_core;
using namespace tlm;

typedef void*   CallbackInstance_t;
typedef void    (AsynCallbackResp_Meth)(CallbackInstance_t instance, int is_write);
typedef void    (AsynCallbackUpdateReq_Meth)(CallbackInstance_t instance);

SC_MODULE(dramsys_conv)
{

    struct req_t
    {
        uint64_t                                      addr;
        uint32_t                                      len;
        int                                           is_write;
    };

    std::list<req_t>                                  all_req_list;
    std::list<req_t>                                  read_req_list;
    std::list<std::pair<req_t, std::queue<uint8_t>>>  out_order_rsp_list;
    std::list<req_t>                                  write_req_list;
    std::deque<uint8_t>                               read_rsp_queue;
    std::queue<int>                                   write_rsp_queue;
    int                                               max_pending_req;
    int                                               inflight_read_cnt;

    //tlm utilities
    tlm_utils::simple_initiator_socket<dramsys_conv>  iSocket;
    tlm_utils::peq_with_cb_and_phase<dramsys_conv>    payloadEventQueue;
    MemoryManager                                     memoryManager;

    //callback function of iSocket
    tlm_sync_enum nb_transport_bw(tlm_generic_payload &payload, tlm_phase &phase, sc_time &bwDelay){
        payloadEventQueue.notify(payload, phase, bwDelay);
      return TLM_ACCEPTED;
    }

    //callback for asynchronize interface
    CallbackInstance_t                                async_callback_instance;
    AsynCallbackResp_Meth*                            async_callback_response_meth;
    AsynCallbackUpdateReq_Meth*                       async_callback_update_request_meth;

    //callback function to deal with
    void peqCallback(tlm_generic_payload &payload,const tlm_phase &phase){
      if (phase == END_REQ)
      {
        req_t req;
        std::list<req_t>::iterator it;
        it = all_req_list.begin();
        req = *it;
        if (payload.get_command() == tlm::TLM_READ_COMMAND ){
            if (req.is_write != 0)
            {
              SC_REPORT_FATAL("CONV", "end read request, but pop write req");
            }
            read_req_list.push_back(req);
            inflight_read_cnt ++;
            // std::cout << sc_time_stamp() <<"  ---- Accept a Read Req -----" << std::endl;
        }
        if (payload.get_command() == tlm::TLM_WRITE_COMMAND){
            if (req.is_write == 0)
            {
              SC_REPORT_FATAL("CONV", "end write request, but pop read req");
            }
            write_req_list.push_back(req);
            // std::cout << sc_time_stamp() <<"  ---- Accept a Write Req -----" << std::endl;
        }
        all_req_list.pop_front();
        if (async_callback_instance && async_callback_update_request_meth)
        {
            async_callback_update_request_meth(async_callback_instance);
        }
      }
      else if (phase == BEGIN_RESP)
      {
        // std::cout << sc_time_stamp() <<"  ---- Response Come-----" << std::endl;
        if (payload.get_command() == tlm::TLM_READ_COMMAND)
        {
          //check if the response can be find in request list
          req_t req;
          {
            std::list<req_t>::iterator it;
            int find_out = 0;
            for (it = read_req_list.begin(); it != read_req_list.end(); ++it)
            {
              req = *it;
              if (req.addr == payload.get_address() && req.len == payload.get_data_length())
              {
                find_out = 1;
                break;
              }
            }
            if (find_out == 0)
            {
              SC_REPORT_FATAL("AXI4_to_TLM", "How could? can not find corresponding request!");
            }
          }

          // std::cout << sc_time_stamp() <<"  ---- Match Read Resp-----" << std::endl;

          std::queue<uint8_t>     rsp_out_order_queue;
          std::pair<req_t, std::queue<uint8_t>> pair;

          unsigned char * data_ptr = (unsigned char*)malloc(payload.get_data_length());
          memcpy(data_ptr, payload.get_data_ptr(), payload.get_data_length());

          for (int i = 0; i < req.len; ++i)
          {
              rsp_out_order_queue.push(data_ptr[i]);
          }
          pair.first = req;
          pair.second = rsp_out_order_queue;
          out_order_rsp_list.push_back(pair);

          free(data_ptr);

          {
            std::list<req_t>::iterator req_it;
            std::list<std::pair<req_t, std::queue<uint8_t>>>::iterator rsp_it;
            while (true)
            {
                if (read_req_list.size() == 0)
                {
                    break;
                }

                req_it = read_req_list.begin();

                int matched = 0;

                //sanity check if out of order is empty
                if (out_order_rsp_list.size() == 0)
                {
                    break;
                }

                for (rsp_it = out_order_rsp_list.begin(); rsp_it != out_order_rsp_list.end(); ++rsp_it)
                {
                    if ( req_it->addr == rsp_it->first.addr && req_it->len == rsp_it->first.len )
                    {
                        matched = 1;
                        break;
                    }
                }

                if (matched == 0)
                {
                    break;
                }

                //push data and erase
                std::queue<uint8_t> oo_queue = rsp_it->second;
                for (uint32_t i = 0; i < req_it->len; ++i)
                {
                    uint8_t byte;
                    byte = oo_queue.front();
                    read_rsp_queue.push_back(byte);
                    oo_queue.pop();
                }
                read_req_list.erase(req_it);
                out_order_rsp_list.erase(rsp_it);
                inflight_read_cnt --;
                if (async_callback_instance && async_callback_response_meth)
                {
                    async_callback_response_meth(async_callback_instance, 0);
                }
            }
          }
        }
        if (payload.get_command() == tlm::TLM_WRITE_COMMAND)
        {
            write_req_list.pop_front();
            write_rsp_queue.push(1);
            if (async_callback_instance && async_callback_response_meth)
            {
                async_callback_response_meth(async_callback_instance, 1);
            }
        }
        payload.release();
        sendToTarget(payload, END_RESP, SC_ZERO_TIME);
      }
      else
      {
          SC_REPORT_FATAL("dramsys_conv", "PEQ was triggered with unknown phase");
      }
    }

    //send request via socket
    void sendToTarget(tlm_generic_payload &payload, const tlm_phase &phase, const sc_time &delay)
    {
      tlm_phase TPhase = phase;
      sc_time TDelay = delay;
      iSocket->nb_transport_fw(payload, TPhase, TDelay);
    }

    //public functions
    int dram_can_accept_req()
    {
        return (all_req_list.size() < max_pending_req);
    }

    void dram_send_req(uint64_t addr, uint64_t length , uint64_t is_write, uint64_t strob_enable, uint8_t * buf, uint8_t * strb_buf)
    {
        req_t req;
        tlm_generic_payload& payload = memoryManager.allocate(length);

        req.addr = addr;
        req.len  = length;
        req.is_write = is_write;
        payload.acquire();
        payload.set_address(addr);
        payload.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        payload.set_dmi_allowed(false);
        if (strob_enable)
        {
            // std::cout << sc_time_stamp() <<"  ---- Write With Strobe-----" << std::endl;
            payload.set_byte_enable_length(length);
            // Allocate a strobe buffer, this strobe buffer will be deleted with payload delete
            auto* strb = new unsigned char[length];
            payload.set_byte_enable_ptr(strb);
            memcpy(payload.get_byte_enable_ptr(), strb_buf , length);
            // std::cout << "Strb: ";
            // for (int i = 0; i < length; ++i)
            // {
            //     if (strb_buf[i] == TLM_BYTE_ENABLED)
            //     {
            //         std::cout << "1,";
            //     } else if (strb_buf[i] == TLM_BYTE_DISABLED)
            //     {
            //         std::cout << "0,";
            //     } else {
            //         std::cout << "Nan,";
            //     }
            // }
            // std::cout << std::endl;
            // std::cout << std::dec << (long long int)payload.get_byte_enable_ptr() << std::endl;
        }else{
            // std::cout << sc_time_stamp() <<"  ---- Without Strobe-----" << std::endl;
            payload.set_byte_enable_length(0);
        }
        payload.set_data_length(length);
        payload.set_streaming_width(length);

        if (is_write)
        {
            memcpy(payload.get_data_ptr(), buf , payload.get_data_length());
            payload.set_command(tlm::TLM_WRITE_COMMAND);
            sendToTarget(payload,tlm::BEGIN_REQ,SC_ZERO_TIME);
            all_req_list.push_back(req);
        } else {
            payload.set_command(tlm::TLM_READ_COMMAND);
            sendToTarget(payload,tlm::BEGIN_REQ,SC_ZERO_TIME);
            all_req_list.push_back(req);
        }

    }

    int dram_has_read_rsp()
    {
        return read_rsp_queue.size();
    }

    int dram_has_write_rsp()
    {
        return write_rsp_queue.size();
    }

    int dram_get_write_rsp()
    {
        write_rsp_queue.pop();
        return 1;
    }

    void dram_get_read_rsp(uint64_t length, uint8_t* buf)
    {
        for (int i = 0; i < length; ++i)
        {
            if (read_rsp_queue.size())
            {
                buf[i] = read_rsp_queue.front();
                read_rsp_queue.pop_front();
            } else {
                buf[i] = 0;
            }
        }
    }

    uint8_t dram_get_read_rsp_byte()
    {
        uint8_t byte;
        if (read_rsp_queue.size())
        {
            byte = read_rsp_queue.front();
            read_rsp_queue.pop_front();
        } else {
            byte = 0;
        }
        return byte;
    }

    uint8_t dram_peek_read_rsp_byte(uint32_t index)
    {
        uint8_t byte;
        if (read_rsp_queue.size() > index)
        {
            byte = read_rsp_queue.at(index);
        } else {
            byte = 0;
        }
        return byte;
    }

    uint8_t dram_pop_read_rsp_byte() {
        uint8_t byte;
        if (read_rsp_queue.size())
        {
            byte = read_rsp_queue.front();
            read_rsp_queue.pop_front();
        } else {
            byte = 0;
        }
        return byte;
    }

    void registerCBInstance(CallbackInstance_t instance)
    {
        async_callback_instance = instance;
    }

    void registerCBRespMeth(AsynCallbackResp_Meth* meth)
    {
        async_callback_response_meth = meth;
    }

    void registerCBUpdateReqMeth(AsynCallbackUpdateReq_Meth* meth)
    {
        async_callback_update_request_meth = meth;
    }


    SC_CTOR(dramsys_conv):
    max_pending_req(1),
    inflight_read_cnt(0),
    memoryManager(true),
    iSocket("socket"),
    async_callback_instance(nullptr),
    async_callback_response_meth(nullptr),
    async_callback_update_request_meth(nullptr),
    payloadEventQueue(this, &dramsys_conv::peqCallback)
    {
        iSocket.register_nb_transport_bw(this, &dramsys_conv::nb_transport_bw);
    }

};
