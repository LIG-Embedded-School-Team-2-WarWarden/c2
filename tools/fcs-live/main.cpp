#include "c2/fcs_client.hpp"
#include "gui/ipc_server.hpp"
#include "assignment_health.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <numbers>
#include <random>
#include <set>
#include <string>
#include <thread>

using Clock = std::chrono::steady_clock;
namespace gui = c2::gui;
namespace fcs = c2::fcs;
static volatile std::sig_atomic_t quitting{};
static void quit(int) { quitting = 1; }
static uint64_t unix_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}
static void put(std::vector<std::byte>& out, uint64_t value, unsigned size) {
    for (unsigned i = size; i > 0; --i) out.push_back(std::byte((value >> ((i - 1) * 8)) & 255));
}
static uint64_t get(std::span<const std::byte> bytes, size_t at, unsigned size) {
    if (at + size > bytes.size()) throw std::invalid_argument("truncated RDR field");
    uint64_t value{};
    for (unsigned i = 0; i < size; ++i) value = (value << 8) | std::to_integer<unsigned>(bytes[at+i]);
    return value;
}
static int signed16(std::span<const std::byte> bytes, size_t at) {
    auto value = int(get(bytes, at, 2)); return value >= 32768 ? value - 65536 : value;
}
struct Target { uint32_t id; double x, y, z; };
struct RdrState {
    bool connected{}, streaming{};
    double pan{}, tilt{};
    std::vector<Target> targets;
    Clock::time_point received{}, frame_received{};
    uint32_t frame{};
};
// The existing fake RDR's local draft v1. Network fields are all big endian.
class FakeRdrInput {
    uint16_t peer_port_;
    std::mutex mutex_;
    RdrState state_;
    uint64_t session_{unix_ms() | 1};
    uint16_t tx_{}, rx_{};
    Clock::time_point hello_{}, heartbeat_{}, stream_{};
    c2::UdpTransport transport_;
    void send(uint16_t kind, const std::vector<std::byte>& body = {}, uint32_t frame = 0) {
        std::vector<std::byte> data;
        put(data, kind, 2); put(data, 1, 2); put(data, 32 + body.size(), 2); put(data, 0, 2);
        put(data, ++tx_, 2); put(data, rx_, 2); put(data, session_, 8);
        put(data, frame, 4); put(data, unix_ms(), 8);
        data.insert(data.end(), body.begin(), body.end());
        transport_.send(data, {"127.0.0.1", peer_port_});
    }
    void receive(std::vector<std::byte> data, const c2::Endpoint& source) {
        if (source.address != "127.0.0.1" || source.port != peer_port_) return;
        std::lock_guard lock(mutex_);
        if (data.size() < 32 || data.size() > 160 || data.size() % 4 ||
            get(data,2,2) != 1 || get(data,4,2) != data.size() || get(data,6,2) ||
            get(data,12,8) != session_) return;
        const auto kind = get(data,0,2);
        const auto now = Clock::now();
        rx_ = uint16_t(get(data,8,2));
        if (kind == 0x0201 && data.size() == 32) state_.connected = true;
        else if (!state_.connected) return;
        else if (kind == 0x0202 && data.size() == 40) {
            state_.pan = signed16(data,32)/100.; state_.tilt = signed16(data,34)/100.;
        } else if (kind == 0x0205 && data.size() == 40 && get(data,34,2) == 0) {
            state_.streaming = true;
        } else if (kind == 0x0203 && data.size() >= 40 && state_.streaming) {
            const auto count = get(data,36,2);
            const auto frame = uint32_t(get(data,20,4));
            const auto captured = get(data,24,8);
            if (count > 10 || data.size() != 40 + count*12 || frame <= state_.frame ||
                captured > unix_ms()+100 || unix_ms()-std::min(unix_ms(),captured) > 2000) return;
            std::vector<Target> targets; std::set<unsigned> ids;
            for (unsigned i = 0; i < count; ++i) {
                auto at = 40 + 12*i;
                auto id = unsigned(get(data,at,2)); auto detected = get(data,at+2,2);
                if (!id || !ids.insert(id).second || detected > 1 || get(data,at+10,2)) return;
                if (detected) targets.push_back({id, signed16(data,at+4)/1000.,
                    signed16(data,at+6)/1000., signed16(data,at+8)/1000.});
            }
            state_.targets = std::move(targets); state_.frame = frame; state_.frame_received = now;
            state_.pan = signed16(data,32)/100.; state_.tilt = signed16(data,34)/100.;
            std::vector<std::byte> body;
            put(body, frame, 4); put(body, state_.targets.size(), 2); put(body, 0, 2);
            send(0x0105, body, frame);
        } else return;
        state_.received = now;
    }
public:
    FakeRdrInput(uint16_t local_port=11000, uint16_t peer_port=1001) : peer_port_(peer_port), transport_({"127.0.0.1", local_port},
        [this](auto data, auto source) { receive(std::move(data), source); }) { transport_.start(); }
    ~FakeRdrInput() { transport_.stop(); }
    void tick() {
        std::lock_guard lock(mutex_);
        auto now = Clock::now();
        if (state_.connected && now-state_.received > std::chrono::seconds(3)) {
            state_ = {}; ++session_; tx_ = rx_ = 0; hello_ = {};
        }
        if (!state_.connected) {
            if (now-hello_ >= std::chrono::seconds(1)) { send(0x0101); hello_ = now; }
        } else {
            if (now-heartbeat_ >= std::chrono::seconds(1)) { send(0x0102); heartbeat_ = now; }
            if (!state_.streaming && now-stream_ >= std::chrono::milliseconds(500)) {
                std::vector<std::byte> body; put(body,1,2); put(body,1000,2);
                send(0x0103, body); stream_ = now;
            }
        }
    }
    RdrState snapshot() {
        std::lock_guard lock(mutex_); auto out = state_;
        if (Clock::now()-out.frame_received > std::chrono::milliseconds(1800)) out.targets.clear();
        return out;
    }
};
static int display_angle(double relative) { return int(std::lround(std::clamp(135-relative, 0., 270.))); }
int main(int argc, char** argv) {
    void* ipc{};
    try {
        fcs::ClientConfig config;
        config.peer = {"10.0.0.3", 2000}; config.local.bind_address = "10.0.0.105";
        config.response_timeout = std::chrono::milliseconds(500);
        config.minimum_elevation = 0; config.maximum_elevation = 60;
        config.minimum_azimuth = 120; config.maximum_azimuth = 240;
        std::filesystem::path stop_file, status_file;
        unsigned rdr_port=1001, rdr_local_port=11000;
        std::wstring ipc_suffix;
        bool original_gui=true;
        for (int i=1; i<argc; ++i) {
            std::string option = argv[i];
            auto next = [&]() -> std::string { if (++i>=argc) throw std::invalid_argument("missing argument"); return argv[i]; };
            if (option == "--bind") config.local.bind_address = next();
            else if (option == "--fcs-ip") config.peer.address = next();
            else if (option == "--fcs-port") { auto p=std::stoi(next()); if(p<1 || p>65535) throw std::invalid_argument("bad port"); config.peer.port=uint16_t(p); }
            else if (option == "--stop-file") stop_file = next();
            else if (option == "--status-file") status_file = next();
            else if (option == "--rdr-port" || option == "--rdr-local-port") {
                auto p=std::stoi(next()); if(p<1 || p>65535) throw std::invalid_argument("bad RDR port");
                (option=="--rdr-port"?rdr_port:rdr_local_port)=unsigned(p);
            }
            else if (option == "--ipc-suffix") { auto s=next(); ipc_suffix.assign(s.begin(),s.end()); }
            else if (option == "--gui-extended") original_gui=false;
            else throw std::invalid_argument("unknown live GUI option");
        }
        std::signal(SIGINT, quit); std::signal(SIGTERM, quit);
        fcs::Client client(config); client.start();
        FakeRdrInput rdr{uint16_t(rdr_local_port),uint16_t(rdr_port)};
        ipc = c2_gui_create((std::wstring(gui::mapping_name)+ipc_suffix).c_str(),
            (std::wstring(gui::mutex_name)+ipc_suffix).c_str(), (std::wstring(gui::pipe_path)+ipc_suffix).c_str());
        if (!ipc) throw std::runtime_error(c2_gui_error());
        std::cout << "LIVE FCS / FAKE RDR / manual Big Endian network codecs, no Protobuf\n"
                  << "GUI FCS-1 = " << config.peer.address << ':' << config.peer.port << '\n'
                  << "Test coordinates: common synthetic origin, FCS forward=180 degrees.\n"
                  << "Assign sends EF03; Fire sends EF05 built-in demo; Cancel sends EF04 OFF.\n"
                  << "Startup sends EF01/EF02 only. Ctrl+C closes this station.\n" << std::flush;
        std::ofstream events;
        if (!status_file.empty()) events.open(status_file.parent_path()/"events.log",std::ios::app);
        auto log=[&](const std::string& message) {
            auto line=std::to_string(unix_ms())+" "+message;
            std::cout<<line<<std::endl;
            if(events) events<<line<<std::endl;
        };
        auto command_result=[&](const char* name,const std::optional<fcs::Packet>& reply) {
            log(std::string("FCS ")+name+(reply ? " reply="+std::to_string(unsigned(reply->result))+
                " sequence="+std::to_string(reply->reply_to) : " response_timeout outcome_unknown"));
            if(!reply) return gui::Result::response_timeout;
            switch(reply->result) {
            case fcs::Result::accepted: return gui::Result::success;
            case fcs::Result::busy: return gui::Result::device_busy;
            case fcs::Result::device_error: return gui::Result::device_fault;
            default: return gui::Result::device_rejected;
            }
        };
        uint32_t target_id{}, fire{}, frame_sequence{};
        AssignmentHealth health; auto& assignment=health.state;
        unsigned logged_state=0, logged_reason=0;
        int commanded_pan{}, commanded_tilt{};
        Target assigned_target{};
        bool demo_requested{}, demo_seen{};
        Clock::time_point next_poll{}, next_log{}, move_deadline{};
        while (!quitting && (stop_file.empty() || !std::filesystem::exists(stop_file))) {
            if (!c2_gui_healthy(ipc)) throw std::runtime_error("GUI IPC stopped");
            rdr.tick();
            if (Clock::now() >= next_poll) {
                bool connected=client.snapshot().connected;
                auto reply=connected?client.query():client.connect();
                if(!reply) log(std::string("FCS ")+(connected?"EF02":"EF01")+" response_timeout");
                next_poll = Clock::now()+std::chrono::seconds(1);
            }
            auto state = client.snapshot(); auto observation = rdr.snapshot();
            bool current = state.connected && state.status &&
                Clock::now()-state.status_received_at < std::chrono::seconds(2);
            uint16_t flags = current ? state.status->flags : 0;
            bool ready = current && !(flags&16) && state.status->azimuth != -32768 && state.status->elevation != -32768;
            auto assigned_now = std::find_if(observation.targets.begin(), observation.targets.end(),
                [&](const Target& t) { return t.id == target_id; });
            bool target_current = target_id && assigned_now != observation.targets.end() &&
                std::abs(assigned_now->x-assigned_target.x)<.01 &&
                std::abs(assigned_now->y-assigned_target.y)<.01 &&
                std::abs(assigned_now->z-assigned_target.z)<.01;
            bool aligned=current && std::abs(state.status->azimuth-commanded_pan)<=1 &&
                std::abs(state.status->elevation-commanded_tilt)<=1;
            health.update(current,ready,assigned_now!=observation.targets.end(),target_current,
                aligned,(flags&2)!=0,demo_requested,Clock::now()>move_deadline);
            if(logged_state!=assignment || logged_reason!=unsigned(health.reason)) {
                log("assignment="+std::to_string(assignment)+" reason="+std::to_string(unsigned(health.reason))+
                    " target="+std::to_string(target_id)+" flags="+std::to_string(flags)+
                    " status_age_ms="+std::to_string(state.status?std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-state.status_received_at).count():-1)+
                    " target_present="+std::to_string(assigned_now!=observation.targets.end())+
                    " target_unchanged="+std::to_string(target_current)+" aligned="+std::to_string(aligned));
                logged_state=assignment; logged_reason=unsigned(health.reason);
            }
            if (demo_requested && current) {
                if (flags&8) demo_seen=true;
                if (demo_seen && ready && assignment==2 && !(flags&14)) { fire=1; demo_requested=false; }
            }
            gui::PipeCommand request; uint64_t ticket;
            if (c2_gui_poll(ipc, &request, &ticket)) {
                gui::PipeResponse response;
                response.request_id = request.request_id; response.command = request.command; response.fcs_id=1;
                auto result = gui::Result::success;
                try {
                    if (!request.request_id || request.fcs_id != 1 || request.rdr_id != 1) result=gui::Result::invalid_request;
                    else if (request.command == 0) { result=health.reason; }
                    else if (request.command == 1) {
                        auto found = std::find_if(observation.targets.begin(), observation.targets.end(),
                            [&](const Target& t) { return t.id == request.target_id; });
                        if (!current) result=gui::Result::status_pending;
                        else if (!ready) result=gui::Result::device_fault;
                        else if (flags&10) result=gui::Result::device_busy;
                        else if (found==observation.targets.end()) result=gui::Result::target_pending;
                        else if (target_id && (target_id!=request.target_id || assignment!=3 || demo_requested)) result=gui::Result::already_assigned;
                        else {
                            const double horizontal=std::hypot(found->x,found->y);
                            if (horizontal<.001) throw std::invalid_argument("undefined target bearing");
                            commanded_pan = int(std::lround(180+std::atan2(found->y,found->x)*180/std::numbers::pi));
                            commanded_tilt = int(std::lround(std::atan2(found->z,horizontal)*180/std::numbers::pi));
                            auto reply=client.point(int16_t(commanded_tilt),int16_t(commanded_pan));
                            result=command_result("EF03",reply);
                            if(result==gui::Result::success) { target_id=request.target_id; assigned_target=*found; health.start(); fire=0; demo_requested=demo_seen=false; move_deadline=Clock::now()+std::chrono::seconds(60); next_poll={}; }
                        }
                    } else if (request.command == 2) {
                        if (!target_id) result=gui::Result::not_assigned;
                        else {
                            auto reply=client.laser(false);
                            result=command_result("EF04_OFF",reply);
                            if(result==gui::Result::success) { target_id=fire=0; health.reset(); demo_requested=demo_seen=false; next_poll={}; }
                        }
                    } else if (request.command == 3) {
                        if (!target_id || target_id!=request.target_id) result=gui::Result::not_assigned;
                        else if (assignment!=2) result=health.reason!=gui::Result::success?health.reason:gui::Result::alignment_required;
                        else if (!current) result=gui::Result::status_pending;
                        else if (!ready) result=gui::Result::device_fault;
                        else if (!target_current) result=gui::Result::target_pending;
                        else if ((flags&10) || demo_requested) result=gui::Result::device_busy;
                        else {
                            auto reply=client.demo();
                            result=command_result("EF05",reply);
                            if(result==gui::Result::success) { fire=0; demo_requested=true; demo_seen=false; next_poll={}; }
                            else if(!reply) { demo_requested=true; health.fail(gui::Result::response_timeout); }
                        }
                    } else result=gui::Result::not_supported;
                } catch (const std::invalid_argument& error) {
                    result=gui::Result::invalid_request; log(std::string("invalid_request ")+error.what());
                } catch (const std::exception& error) {
                    result=gui::Result::device_error; log(std::string("device_exception ")+error.what());
                }
                response.result=uint32_t(result); response.target_id=target_id;
                response.assignment=assignment; response.fire=fire; response.connected=state.connected;
                // Upstream GUI v2 accepts result 0..6 and assignment 0..3 only.
                // Keep detailed reasons internally and in events.log.
                if(original_gui) {
                    if(response.assignment==4) response.assignment=1;
                    if(request.command==0) response.result=0;
                    else if(response.result>=7) response.result=
                        (result==gui::Result::response_timeout || result==gui::Result::device_rejected ||
                         result==gui::Result::device_fault)?6:2;
                }
                c2_gui_reply(ipc,&response,ticket);
                if(request.command) log("GUI command="+std::to_string(request.command)+" result="+std::to_string(response.result)+" target="+std::to_string(target_id));
            }
            gui::Frame frame; frame.header.sequence=++frame_sequence; frame.header.timestamp_ms=unix_ms();
            auto& p=frame.payload; p.rdr_count=p.fcs_count=p.selected_rdr=p.selected_fcs=1;
            p.rdr.id=p.fcs.id=1; p.rdr.connected=p.rdr.enabled=observation.connected;
            // Pinned GUI v2 uses isOperating as operation availability, not MOVING.
            p.rdr.operating=observation.connected; p.rdr.azimuth=display_angle(observation.pan); p.rdr.elevation=int(observation.tilt);
            p.fcs.connected=state.connected; p.fcs.enabled=ready; p.fcs.operating=ready;
            p.fcs.laser_on=current && (flags&4); p.fcs.azimuth=current ? state.status->azimuth : 0;
            p.fcs.elevation=current && state.status->elevation!=-32768 ? state.status->elevation : 0;
            for (const auto& t:observation.targets) {
                if(p.target_count==gui::target_capacity) break;
                auto& out=p.targets[p.target_count++]; out.id=t.id; out.rdr_id=1; out.valid=1;
                out.azimuth=display_angle(std::atan2(t.y,t.x)*180/std::numbers::pi);
                out.elevation=int(std::lround(std::atan2(t.z,std::hypot(t.x,t.y))*180/std::numbers::pi));
                out.distance=float(std::sqrt(t.x*t.x+t.y*t.y+t.z*t.z));
                out.x=float(-t.y); out.y=float(t.x); out.z=float(t.z);
            }
            if (!c2_gui_publish(ipc,&frame)) throw std::runtime_error("GUI frame publish failed");
            if (Clock::now()>=next_log) {
                std::cout << "FCS connected=" << state.connected << " ready=" << ready << " flags=" << flags
                          << " RDR connected=" << observation.connected << " targets=" << p.target_count << '\n' << std::flush;
                if (!status_file.empty()) {
                    std::ofstream out(status_file);
                    out << "{\"fcs_connected\":" << (state.connected?"true":"false")
                        << ",\"fcs_ready\":" << (ready?"true":"false") << ",\"flags\":" << flags
                        << ",\"rdr_connected\":" << (observation.connected?"true":"false")
                        << ",\"targets\":" << p.target_count << ",\"assignment\":" << assignment
                        << ",\"assignment_reason\":" << unsigned(health.reason) << ",\"assigned_target\":" << target_id
                        << ",\"timestamp_ms\":" << unix_ms() << "}";
                }
                next_log=Clock::now()+std::chrono::seconds(2);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        c2_gui_destroy(ipc); ipc=nullptr; client.stop();
        return 0;
    } catch(const std::exception& error) {
        if (ipc) c2_gui_destroy(ipc);
        std::cerr << "Live station error: " << error.what() << '\n'; return 1;
    }
}
