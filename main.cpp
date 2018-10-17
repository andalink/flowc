
#include <algorithm>
#include <filesystem>
#include <vector>

#include "include/crow.h"
#include "include/common.h"
#include "include/container.h"
#include "include/field.h"
#include "include/settings.h"


//std::vector<flow_data>::iterator filter_timestamp(std::vector<flow_data>::iterator begin, std::vector<flow_data>::iterator end, uint32_t from, uint32_t to)
//{
//    return std::remove_if(begin, end, [&from, &to](flow_data& flow){return flow.timestamp < from || flow.timestamp >= to;});
//}

std::vector<flow_data>::iterator filter_ip_src_addr(std::vector<flow_data>::iterator begin, std::vector<flow_data>::iterator end, uint32_t value)
{
    return std::remove_if(begin, end, [&value](flow_data& flow){return flow.ip_src_addr != value;});
}

std::vector<flow_data>::iterator filter_ip_dst_addr(std::vector<flow_data>::iterator begin, std::vector<flow_data>::iterator end, uint32_t value)
{
    return std::remove_if(begin, end, [&value](flow_data& flow){return flow.ip_dst_addr != value;});
}

std::vector<flow_data>::iterator filter_postnat_src_addr(std::vector<flow_data>::iterator begin, std::vector<flow_data>::iterator end, uint32_t value)
{
    return std::remove_if(begin, end, [&value](flow_data& flow){return flow.postnat_src_addr != value;});
}

std::vector<flow_data> filter_data(std::string directory, uint32_t start_from, uint32_t end_at,
                                   uint32_t ip_src_addr, uint32_t ip_dst_addr, uint32_t postnat_src_addr)
{
    std::vector<std::filesystem::directory_entry> files;
    std::filesystem::directory_iterator dir(directory);
    copy_if(std::filesystem::begin(dir), std::filesystem::end(dir), std::back_inserter(files),
        [](const std::filesystem::directory_entry& entry)
        {
            return entry.status().type() == std::filesystem::file_type::regular &&
                    entry.path().extension().string() == OUTPUT_EXTENSION &&
                    std::regex_search(entry.path().stem().string(), std::regex("^\\d{8}T\\d{6}$"));
        });

    std::sort(files.begin(), files.end(),
        [](std::filesystem::directory_entry& entry1, std::filesystem::directory_entry& entry2)
        {
            return entry1.path().stem().string() < entry2.path().stem().string();
        });

    std::vector<std::filesystem::directory_entry>::iterator first_file = std::max_element(files.begin(), files.end(),
        [&start_from](std::filesystem::directory_entry& entry1, std::filesystem::directory_entry& entry2)
        {
            return entry1.path().stem().string() <= timestamp_to_timestr(start_from) &&
                   entry2.path().stem().string() <= timestamp_to_timestr(start_from) &&
                   entry1.path().stem().string() < entry2.path().stem().string();
        });

    std::vector<std::filesystem::directory_entry>::iterator last_file = std::max_element(first_file, files.end(),
        [&end_at](std::filesystem::directory_entry& entry1, std::filesystem::directory_entry& entry2)
        {
            return entry1.path().stem().string() < timestamp_to_timestr(end_at) &&
                   entry2.path().stem().string() < timestamp_to_timestr(end_at) &&
                   entry1.path().stem().string() < entry2.path().stem().string();
        });

    std::vector<flow_data> result;
    for(std::vector<std::filesystem::directory_entry>::iterator it = first_file; it <= last_file; ++it)
    {
        container cont;
        cont.open_file(it->path().string());

        std::vector<flow_data> flows;
        while(true)
        {
            flows = cont.read_flows(256, start_from, end_at);
            if(flows.size() == 0)
            {
                break;
            }
//            std::vector<flow_data>::iterator filter_end = filter_timestamp(flows.begin(), flows.end(), start_from, end_at);
//            if(filter_end == flows.begin())
//                continue;

            std::vector<flow_data>::iterator filter_end = flows.end();
            if(ip_src_addr != 0)
                filter_end = filter_ip_src_addr(flows.begin(), filter_end, ip_src_addr);
            if(ip_dst_addr != 0)
                filter_end = filter_ip_dst_addr(flows.begin(), filter_end, ip_dst_addr);
            if(postnat_src_addr != 0)
                filter_end = filter_postnat_src_addr(flows.begin(), filter_end, postnat_src_addr);
            flows.erase(filter_end, flows.end());

            result.reserve(result.size() + flows.size());
            result.insert(result.end(), flows.begin(), flows.end());
            //break;
        }
    }
    return result;
}

int main(int argc, const char *argv[])
{
    std::string directory = OUTPUT_DIRECTORY;
    uint16_t flowc_port = FLOWC_PORT;

    try
    {
        settings cfg = settings::load_config(CONFIG_NAME);
        directory = cfg.output_directory();
        flowc_port = cfg.flowc_port();
    }
    catch (const std::exception &e)
    {
        std::cout << "error while loading config file " << e.what() << std::endl;
    }
    crow::SimpleApp app;

    CROW_ROUTE(app, "/")
    ([]()
    {
        std::cout << "char size is " << sizeof(char) << "\n"
                  << "short size is " << sizeof(short) << "\n"
                  << "int size is " << sizeof(int) << "\n"
                  << "long size is " << sizeof(long) << "\n"
                  << "long long size is " << sizeof(long long) << "\n"
                  << "streamof size is " << sizeof(std::streamoff) << std::endl;

        return crow::response(404);
    });

    CROW_ROUTE(app, "/convert")
    ([](const crow::request& req)
    {
        crow::json::wvalue data;
        if(req.url_params.get("ipstr") != nullptr)
        {
            data["ipnum"] = std::to_string(ipstr_to_ipnum(req.url_params.get("ipstr")));
        }
        if(req.url_params.get("ipnum") != nullptr)
        {
            data["ipstr"] = ipnum_to_ipstr(std::stoul(req.url_params.get("ipnum")));
        }
        if(req.url_params.get("timestr") != nullptr)
        {
            data["timestamp"] = std::to_string(timestr_to_timestamp(req.url_params.get("timestr")));
        }
        if(req.url_params.get("timestamp") != nullptr)
        {
            data["timestr"] = timestamp_to_timestr(std::stoul(req.url_params.get("timestamp")));
        }

        return crow::response(data);
    });

    CROW_ROUTE(app, "/log")
    ([&directory](const crow::request& req)
    {
        crow::json::wvalue data;
        try
        {
            uint32_t start_from = req.url_params.get("from") == nullptr ? 0 : timestr_to_timestamp(req.url_params.get("from"));
            uint32_t end_at = req.url_params.get("to") == nullptr ? UINT32_MAX : timestr_to_timestamp(req.url_params.get("to"));
            uint32_t ip_src_addr = req.url_params.get("src") == nullptr ? 0 : ipstr_to_ipnum(req.url_params.get("src"));
            uint32_t ip_dst_addr = req.url_params.get("dst") == nullptr ? 0 : ipstr_to_ipnum(req.url_params.get("dst"));
            uint32_t postnat_src_addr = req.url_params.get("postnat") == nullptr ? 0 : ipstr_to_ipnum(req.url_params.get("postnat"));


            unsigned int idx = 0;
            for(flow_data& item: filter_data(directory, start_from, end_at, ip_src_addr, ip_dst_addr, postnat_src_addr))
            {
                crow::json::wvalue& json_item = data[idx];
                json_item["time"] = timestamp_to_timestr(item.timestamp);
                json_item["src"] = ipnum_to_ipstr(item.ip_src_addr);
                json_item["dst"] = ipnum_to_ipstr(item.ip_dst_addr);
                json_item["postnat"] = ipnum_to_ipstr(item.postnat_src_addr);
                idx++;
            }
        }
        catch (const std::exception &e)
        {
            std::cout << "error in parameters " << e.what() << std::endl;
            return crow::response(400);
        }
        return crow::response(data);
    });

    CROW_ROUTE(app, "/test")
    ([&directory](const crow::request& req)
    {
        crow::json::wvalue data;

        std::map<std::string, std::size_t> test_case{{"20181016T124836", 0x10},
                                                     {"20181016T124837", 0x72a0},
                                                     {"20181016T124838", 0x2b0f0},
                                                     {"20181016T124839", 0x4c780},
                                                     {"20181016T124840", 0x68ed0},
                                                     {"20181016T124841", 0x78530}};



        std::cout << std::hex;
        for(std::pair<std::string, std::size_t> val: test_case)
        {
            filter_data(directory, timestr_to_timestamp(val.first), UINT32_MAX, 0, 0, 0);
            std::cout << val.first << " " << val.second << std::endl;
            std::cout << "------------------------------------------\n" << std::endl;
        }
        return crow::response(data);
    });

    //app.loglevel(crow::LogLevel::Warning);
    app.port(flowc_port).run();
    return 0;
}


//    std::time_t file_int = 4;

//    boost::iterator_range<std::filesystem::directory_iterator> it =
//    boost::make_iterator_range(std::filesystem::begin(dir), std::filesystem::end(dir));

//    copy(
//        it
//            | filtered([](const std::filesystem::directory_entry& entry) {
//                return entry.status().type() == std::filesystem::file_type::regular &&
//                       entry.path().extension().string() == OUTPUT_EXTENSION &&
//                       std::regex_search(entry.path().stem().string(), std::regex("^\\d{8}T\\d{6}$"));}),
//        std::back_inserter(files));

//    std::filesystem::directory_iterator beg =


//    std::for_each(std::filesystem::begin(dir), std::filesystem::end(dir),
//        [](const std::filesystem::directory_entry& entry)
//        {
//            std::cout << entry.path() << std::endl;
//        });

/*
void data_preparation_thread()
{
    udp::socket& socket_;
    boost::asio::io_service&  io_service_;
    udp::endpoint sender_endpoint_;

    while(true) //more_data_to_prepare())
    {
        data_chunk const data=prepare_data();
        std::lock_guard<std::mutex> lk(mut);
        data_queue.push(data);
        data_cond.notify_one();
    }
}

void data_processing_thread()
{
    while(true)
    {
    std::unique_lock<std::mutex> lk(mut);
    data_cond.wait(
        lk,[]{return !data_queue.empty();});
    data_chunk data=data_queue.front();
    data_queue.pop();
    lk.unlock();
    process(data);
    if(is_last_chunk(data))
        break;
    }
}
*/


/*
class handler
{
public:
  handler(boost::asio::io_service& io_serv, udp::socket& flow_socket)
    : io_service_(io_serv), socket_(flow_socket)
  {
    //do_receive();
     // io_service_.run();
  }

  void do_receive()
  {
    std::cout << "thread" << std::endl;
    socket_.async_receive_from(
        boost::asio::buffer(data_, max_length), sender_endpoint_,
        [this](boost::system::error_code ec, std::size_t bytes_recvd)
        {
        std::cout << "lambda" << std::endl;

          if (!ec && bytes_recvd > 0)
          {
              //for(unsigned char& byte : data_)
              for(int i = 0; i < bytes_recvd; ++i)
                  std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)data_[i]; //(int)byte;
              std::cout << std::endl;
          }
          do_receive();
        });
  }

private:
  udp::socket& socket_;
  boost::asio::io_service&  io_service_;
  udp::endpoint sender_endpoint_;
  enum { max_length = 1024 };
  std::array<unsigned char, max_length> data_;
};

void thread_function(handler &hdl)
{
     hdl.do_receive();
}

int main(int argc, char* argv[])
{
    int port = 2055;
    boost::asio::io_service io_service;
    udp::socket flow_socket(io_service, udp::endpoint(boost::asio::ip::address_v4::any(), port));


    handler h1(io_service, flow_socket);
    handler h2(io_service, flow_socket);
    std::thread t1(std::bind(&handler::do_receive, h1));
    std::thread t2(std::bind(&handler::do_receive, h2));

    //std::thread t1(thread_function, std::ref(h1));
    //std::thread t2(thread_function, std::ref(h2));

    io_service.run();


    t1.join();
    t2.join();

//std::thread t(thread_function, std::ref(h));
//std::thread t(std::bind(&handler::process, &h));



    return 0;
}*/

/*
const unsigned int IPFIX_HDR_LENGTH = 16;


std::mutex mut;
//std::queue<data_chunk> data_queue;
std::condition_variable data_cond;


void print_buffer(boost::asio::streambuf buff) {
    for(auto& byte : buff.data())
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)byte;
    std::cout << std::endl;
}

void print_buffer(std::vector<unsigned char> buff) {
    for(unsigned char& byte : buff)
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)byte;
    std::cout << std::endl;
}
*/

/*
int main()
{
    //std::array<unsigned char, IPFIX_HDR_LENGTH> hdr_buff;

    boost::asio::io_service io_service;
    int port = 2055;
    udp::endpoint flow_source = udp::endpoint(boost::asio::ip::address_v4::any(), port);
    boost::asio::ip::udp::socket socket = boost::asio::ip::udp::socket(io_service, flow_source);
    io_service.run();


    boost::asio::streambuf hdr_buff;
    hdr_buff.prepare(IPFIX_HDR_LENGTH);

    // reserve 512 bytes in output sequence
    //boost::asio::streambuf::mutable_buffers_type hdr_buff = b.prepare(IPFIX_HDR_LENGTH);

    // received data is "committed" from output sequence to input sequence
    //b.commit(n);



    //socket.receive_from(boost::asio::buffer(hdr_buff, IPFIX_HDR_LENGTH), flow_source);
    socket.receive(hdr_buff);
    print_buffer(hdr_buff);

    int version = (hdr_buff[0] << 8) | hdr_buff[1];
    int length = (hdr_buff[2] << 8) | hdr_buff[3];
    std::cout << std::dec << version << " " << length << std::endl;

    std::cout << socket.available() << std::endl;


    std::vector<unsigned char> buff;
    length -= IPFIX_HDR_LENGTH;
    buff.resize(length);
    //socket.receive_from(boost::asio::buffer(buff.data(), length), flow_source);
    socket.receive(boost::asio::buffer(buff.data(), length));

    //buff.resize(length);
    print_buffer(buff);


//    try
//    {
//        int i = 0;
//        while(i < 10000000)
//        {
//            socket.async_receive_from(boost::asio::buffer(handler.data(), MAX_LENGTH), flow_source, handler);
//            i++;
//        }
//    }
//    catch (std::exception& e)
//    {
//      std::cerr << "Exception: " << e.what() << "\n";
//    }


//    while(True)
//    {
//        data_chunk const data=prepare_data();
//        std::lock_guard<std::mutex> lk(mut);
//        data_queue.push(data);
//        data_cond.notify_one();
//    }
}*/

//void data_processing_thread()
//{
//    while(true)
//    {
//        std::unique_lock<std::mutex> lk(mut);
//        data_cond.wait(lk, []{return !data_queue.empty();});
//        data_chunk data=data_queue.front();
//        data_queue.pop();
//        lk.unlock();
//        process(data);
//        if(is_last_chunk(data))
//            break;
//    }
//}


/*

class handler
{
public:
    handler(std::condition_variable& task_done, int interval)
        : task_done_(task_done), interval_(interval)
    {}

    void process()
    {
        while(true)
        {
            std::unique_lock<std::mutex> lk(handler_mutex_);
            std::cout << interval_ << " Waiting... \n" << std::endl;
            data_ready_.wait(lk, [this](){return notified_;});
            is_busy_ = true;
            std::cout << interval_ << " Signal accuired, performing some actions... \n" << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(interval_ * 1000));
            std::cout << interval_ << " Notified! \n" << std::endl;
            is_busy_ = false;
            task_done_.notify_all();
        }
    }

    void notify()
    {
        notified_ = true;
        data_ready_.notify_one();
        notified_ = false;
    }

    bool is_busy() const
    {
        return is_busy_;
    }

private:
    int interval_;
    bool notified_ = false;
    bool is_busy_ = false;
    std::condition_variable data_ready_;
    std::condition_variable& task_done_;
    std::mutex handler_mutex_;
};



void thread_function(handler &hdl)
{
     hdl.process();
}

int main(int argc, char* argv[])
{
    std::mutex mtx;
    std::unique_lock<std::mutex> lk(mtx);
    std::condition_variable task_done;

    handler h1(task_done, 1);
    handler h2(task_done, 2);
    handler h3(task_done, 3);
    handler h4(task_done, 4);
    std::vector<handler> hdls;
    hdls.push_back(h1);
    hdls.push_back(h2);
    hdls.push_back(h3);
    hdls.push_back(h4);

    //const std::array<handler&, 4> hdls{handler(task_done, 1), handler(task_done, 2), handler(task_done, 3), handler(task_done, 4)};
    std::vector<std::thread> trds;
    for(const handler& hdl : hdls)
    {
        std::thread t(std::bind(&handler::process, hdl));
        std::cout << "Thread started \n" << std::endl;
        trds.push_back(t);
    }

    //std::thread t(thread_function, std::ref(h));
    //std::thread t(std::bind(&handler::process, &h));

    while(true)
    {
        if(!std::any_of(hdls.begin(), hdls.end(), [](const handler& hdl){return hdl.is_busy();}))
        {
            std::cout << "Handler is busy, whaiting... \n" << std::endl;
            task_done.wait(lk);
        }
        for(handler& hdl : hdls)
            if(!hdl.is_busy())
            {
                std::cout << "Sending notification \n" << std::endl;
                hdl.notify();
                std::cout << "Notification sent \n" << std::endl;
                break;
            }
    }

    for(std::thread& trd : trds)
        trd.join();

}
*/
/*
class IpfixHandler
{
public:

    int operator()(boost::system::error_code ec, std::size_t bytes_recvd) const
    {
        std::cout << "received  " << std::endl;
        if (!ec && bytes_recvd > 0)
        {
          std::cout << bytes_recvd << ": " << std::endl;

          for(int i = 0 ; i < bytes_recvd; ++i ){
                if(i > 0 && i % 16 == 0)
                    std::cout << std::endl;
                else if(i > 0 && i % 8 == 0)
                    std::cout << "  ";
                std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)data_[i] << ' ';
          }
          std::cout << std::endl;
         }
    }
    std::array<unsigned char, MAX_LENGTH>& data()
    {
        return data_;
    }
private:
    std::array<unsigned char, MAX_LENGTH> data_;
    boost::asio::ip::udp::endpoint sender_endpoint_;
};



int main(int argc, char* argv[])
{
    boost::asio::io_service io_service;
    int port = 2055;
    udp::endpoint flow_source = udp::endpoint(boost::asio::ip::address_v4::any(), port);
    boost::asio::ip::udp::socket socket = boost::asio::ip::udp::socket(io_service, flow_source);
    IpfixHandler handler = IpfixHandler();

    io_service.run();

    try
    {
        int i = 0;
        while(i < 10000000)
        {
            socket.async_receive_from(boost::asio::buffer(handler.data(), MAX_LENGTH), flow_source, handler);
            i++;
        }
    }
    catch (std::exception& e)
    {
      std::cerr << "Exception: " << e.what() << "\n";
    }

    return 0;
} */

//  try
//  {
////    if (argc != 2)
////    {
////      std::cerr << "Usage: async_udp_echo_server <port>\n";
////      return 1;
////    }

//    boost::asio::io_service io_service;
//    int port = 2055;
//    server s(io_service, port);

//    io_service.run();
//  }
//  catch (std::exception& e)
//  {
//    std::cerr << "Exception: " << e.what() << "\n";
//  }

//  return 0;


//class server
//{
//  public:
//    server(boost::asio::io_service& io_service, short port)
//      : socket_(io_service, udp::endpoint(boost::asio::ip::address_v4::any(), port))
//    {
//      //socket_.open(udp::v4());
//      do_receive();
//    }

//    void do_receive()
//    {
//      std::cout << "do_receive" << std::endl;
//      socket_.async_receive_from(
//      boost::asio::buffer(data_, max_length), sender_endpoint_,
//        [this](boost::system::error_code ec, std::size_t bytes_recvd)
//        {
//          std::cout << "received" << std::endl;
//          if (!ec && bytes_recvd > 0)
//          {
//            std::cout << bytes_recvd << ": " << std::endl;

//            for(int i = 0 ; i < bytes_recvd; ++i ){
//                  if(i > 0 && i % 16 == 0)
//                      std::cout << std::endl;
//                  else if(i > 0 && i % 8 == 0)
//                      std::cout << "  ";
//                  std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)data_[i] << ' ';
//            }
//            std::cout << std::endl;
//            do_receive();
//          }
//          else
//          {
//            do_receive();
//          }
//        });
//    }

//  private:
//    boost::asio::ip::udp::socket socket_;
//    boost::asio::ip::udp::endpoint sender_endpoint_;
//    enum { max_length = 1024 };
//    unsigned char data_[max_length];
//};

/*
using namespace boost::asio;
io_service service;
ip::udp::socket sock(service);
boost::asio::ip::udp::endpoint sender_ep;
char buff[512];
void on_read(const boost::system::error_code & err, std::size_t read_bytes)
 {
    std::cout << "do_receive" << std::endl;
    std::cout << "read " << read_bytes << std::endl;
    sock.async_receive_from(buffer(buff), sender_ep, on_read);
}

int main(int argc, char* argv[])
{
    ip::udp::endpoint ep(boost::asio::ip::address_v4::any(), 2055 );
    sock.open(ep.protocol());
    sock.set_option(boost::asio::ip::udp::socket::reuse_address(true));
    //sock.set_option(boost::asio::ip::udp::socket::implementation_type);
    sock.bind(ep);
    sock.async_receive_from(buffer(buff, 512), sender_ep, on_read);
    service.run();
}*/

/*
#include<sys/socket.h>
#define BUFLEN 512  //Max length of buffer
#define PORT 2055

void die(char *s)
{
    perror(s);
    exit(1);
}

int main(int argc, char* argv[])
{
    struct sockaddr_in si_me, si_other;

    int s, i, recv_len;
    socklen_t slen = sizeof(si_other);
    char buf[BUFLEN];

    //create a UDP socket
    if ((s=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
    {
        die("socket");
    }

    // zero out the structure
    memset((char *) &si_me, 0, sizeof(si_me));

    si_me.sin_family = AF_INET;
    si_me.sin_port = htons(PORT);
    si_me.sin_addr.s_addr = htonl(INADDR_ANY);

    //bind socket to port
    if( bind(s , (struct sockaddr*)&si_me, sizeof(si_me) ) == -1)
    {
        die("bind");
    }

    //keep listening for data
    while(1)
    {
        printf("Waiting for data...");
        fflush(stdout);

        //try to receive some data, this is a blocking call
        if ((recv_len = recvfrom(s, buf, BUFLEN, 0, (struct sockaddr *) &si_other, &slen)) == -1)
        {
            die("recvfrom()");
        }

        //print details of the client/peer and the data received
        printf("Received packet from %s:%d\n", inet_ntoa(si_other.sin_addr), ntohs(si_other.sin_port));
        printf("Data: %s\n" , buf);
    }

    close(s);
    return 0;


}
*/

//#include <iostream>

//using namespace std;

//int main()
//{
//    cout << "Hello World!" << endl;
//    return 0;
//}

/*
std::vector<int> data({16, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20});
int data_index = 0;


void read_data(void* buf_ptr, int length)
{

    for(int i = data_index; i < data_index + length; ++data_index)
    {
        *(int*)buf_ptr = data[i];
        buf_ptr++;
    }
}

int handler(boost::system::error_code ec, std::size_t bytes_recvd)
{

}

int main(int argc, char* argv[])
{
    expandable_buffer<int, MAX_LENGTH> ex_buf;
    read_data(ex_buf.begin(), MAX_LENGTH);
    ex_buf.print();

    boost::asio::io_service io_service;
    int port = 2055;
    udp::endpoint flow_source = udp::endpoint(boost::asio::ip::address_v4::any(), port);
    boost::asio::ip::udp::socket socket = boost::asio::ip::udp::socket(io_service, flow_source);
    io_service.run();

    socket.async_receive_from(boost::asio::buffer((void*)ex_buf.begin(), MAX_LENGTH), flow_source, handler);
}
*/
