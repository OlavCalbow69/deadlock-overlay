#pragma once
#include "focus_view.h"
#include "makcu_input.h"
#include <iomanip>
#include <fstream>
#include <sstream>

namespace overlay {
struct CalibrationRecord {
    uint64_t id{};int hero{};bool sniper{};
    float horizontal{},vertical{},aspect{},counts_x{},counts_y{};
    float sensitivity{},fov_adjust{};bool weapon_zoom{},scaling_verified{};
};
inline bool calibration_view(const CalibrationRecord& r,const Snapshot& s,bool sniper) {
    auto close=[](float a,float b){return std::isfinite(a)&&std::isfinite(b)&&a>0&&b>0&&std::abs(a-b)/a<=.02f;};
    return s.hero.valid&&s.projection.valid&&r.hero==s.hero.id&&r.sniper==sniper
        &&close(r.horizontal,s.projection.horizontal_fov)&&close(r.vertical,s.projection.vertical_fov)&&close(r.aspect,s.projection.aspect);
}
struct CalibrationMatch {
    const CalibrationRecord* record{};
    float counts_x{},counts_y{};
    bool slide_compensated{};
    bool sensitivity_scaled{};
    explicit operator bool()const{return record!=nullptr;}
    const CalibrationRecord* operator->()const{return record;}
    const char* description()const{return sensitivity_scaled?"Automatic response / verified live sensitivity":slide_compensated?"Sliding / stationary calibration with live sensitivity":"Matching calibration available";}
};
class CalibrationStore {
    std::filesystem::path path_;std::vector<CalibrationRecord> records_;uint64_t next_{1};
    struct Binding {uint64_t id{};bool zoom{};float adjust{},base{};double observed{};};
    std::vector<Binding> bindings_;
    uint64_t pending_id_{};bool pending_zoom_{};ProjectionInfo pending_view_{};float pending_adjust_{};
    double pending_since_{},slide_until_{};int slide_hero_{};bool slide_sniper_{},slide_zoom_{};
    const CalibrationRecord* exact(const Snapshot& s,bool sniper)const {
        const CalibrationRecord* best=nullptr;
        for(const auto& r:records_)if(calibration_view(r,s,sniper)
            &&(!r.sensitivity||(s.view_input.valid&&r.weapon_zoom==s.view_input.weapon_zoom&&std::abs(r.sensitivity-s.view_input.mouse_sensitivity)/r.sensitivity<=.02f))
            &&(!best||r.id>best->id))best=&r;return best;
    }
    static bool valid(const CalibrationRecord& r) {
        auto gain=[](float v){return std::isfinite(v)&&std::abs(v)>=.01f&&std::abs(v)<=100000.f;};
        return r.id&&r.hero>0&&std::isfinite(r.horizontal)&&r.horizontal>1&&r.horizontal<179
            &&std::isfinite(r.vertical)&&r.vertical>0&&r.vertical<179&&std::isfinite(r.aspect)&&r.aspect>.1f&&r.aspect<10&&gain(r.counts_x)&&gain(r.counts_y)
            &&std::isfinite(r.sensitivity)&&r.sensitivity>=0&&r.sensitivity<100000&&std::isfinite(r.fov_adjust)&&r.fov_adjust>=0&&r.fov_adjust<64
            &&(!r.scaling_verified||(r.sensitivity>0&&r.fov_adjust>0));
    }
    bool write_records(const std::vector<CalibrationRecord>& next) {
        std::error_code ec;std::filesystem::create_directories(path_.parent_path(),ec);if(ec)return false;
        auto temp=path_;temp+=L".tmp";std::ofstream out(temp,std::ios::trunc);if(!out)return false;
        out<<"ADAPTIVE_CALIBRATION_V2\n"<<std::setprecision(9);
        for(const auto& r:next)out<<r.id<<' '<<r.hero<<' '<<r.sniper<<' '<<r.horizontal<<' '<<r.vertical<<' '<<r.aspect<<' '<<r.counts_x<<' '<<r.counts_y
            <<' '<<r.sensitivity<<' '<<r.fov_adjust<<' '<<r.weapon_zoom<<' '<<r.scaling_verified<<'\n';
        out.flush();if(!out)return false;out.close();
        return MoveFileExW(temp.c_str(),path_.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
    }
public:
    explicit CalibrationStore(std::filesystem::path path):path_(std::move(path)) {
        std::ifstream in(path_);std::string line;if(!std::getline(in,line))return;
        const bool version2=line=="ADAPTIVE_CALIBRATION_V2";if(!version2&&line!="ADAPTIVE_CALIBRATION_V1")return;
        while(records_.size()<128&&std::getline(in,line)) {
            if(line.size()>512)continue;std::istringstream row(line);CalibrationRecord r;int sniper{};
            if(row>>r.id>>r.hero>>sniper>>r.horizontal>>r.vertical>>r.aspect>>r.counts_x>>r.counts_y) {
                if(version2){int zoom{},verified{};if(!(row>>r.sensitivity>>r.fov_adjust>>zoom>>verified)||(zoom!=0&&zoom!=1)||(verified!=0&&verified!=1))continue;r.weapon_zoom=zoom!=0;r.scaling_verified=verified!=0;}
                if((sniper!=0&&sniper!=1)||!valid(r)||r.id==UINT64_MAX)continue;r.sniper=sniper!=0;
                records_.push_back(r);next_=std::max(next_,r.id+1);
            }
        }
    }
    void observe(const Snapshot& s,bool sniper,double now) {
        const auto& view=s.view_input;
        if(!s.hero.valid||!s.projection.valid||!s.local_alive||s.replay||!view.valid||!s.time||GetTickCount64()<s.time
            ||GetTickCount64()-s.time>50||s.projection_ms<=0||now-s.projection_ms>50) {
            pending_id_=0;slide_until_=0;return;
        }
        if(slide_hero_!=s.hero.id||slide_sniper_!=sniper||slide_zoom_!=view.weapon_zoom)slide_until_=0;
        slide_hero_=s.hero.id;slide_sniper_=sniper;slide_zoom_=view.weapon_zoom;
        if(view.sliding){slide_until_=now+750;pending_id_=0;return;}
        const auto* record=exact(s,sniper);
        if(!record){pending_id_=0;return;}
        auto changed=[](float a,float b){return a<=0||!std::isfinite(a)||std::abs(a-b)/a>.002f;};
        if(pending_id_!=record->id||pending_zoom_!=view.weapon_zoom||changed(pending_view_.horizontal_fov,s.projection.horizontal_fov)
            ||changed(pending_view_.vertical_fov,s.projection.vertical_fov)||changed(pending_view_.aspect,s.projection.aspect)
            ||changed(pending_adjust_,view.fov_sensitivity)) {
            pending_id_=record->id;pending_zoom_=view.weapon_zoom;pending_view_=s.projection;pending_adjust_=view.fov_sensitivity;pending_since_=now;return;
        }
        if(now-pending_since_<250)return;
        const float base=view.mouse_sensitivity/view.fov_sensitivity;
        for(auto& binding:bindings_)if(binding.id==record->id) {
            // A changed base sensitivity needs explicit recalibration, as before.
            if(binding.zoom==view.weapon_zoom&&std::abs(base-binding.base)/binding.base<=.02f)binding.observed=now;
            return;
        }
        if(bindings_.size()>=128)bindings_.erase(bindings_.begin());
        bindings_.push_back({record->id,view.weapon_zoom,view.fov_sensitivity,base,now});
    }
    CalibrationMatch find(const Snapshot& s,bool sniper)const {
        const auto& view=s.view_input;
        // Reuse a measured baseline only after another independently measured
        // view confirmed that the live sensitivity predicts both axis gains.
        if(s.hero.valid&&s.projection.valid&&view.valid&&!s.replay) {
            const CalibrationRecord* best=nullptr;
            for(const auto& r:records_)if(r.hero==s.hero.id&&r.sniper==sniper&&r.scaling_verified&&(!best||r.id>best->id))best=&r;
            if(best) {
                const float ratio=best->sensitivity/view.mouse_sensitivity;
                if(std::isfinite(ratio)&&ratio>=.25f&&ratio<=4.f)return {best,best->counts_x*ratio,best->counts_y*ratio,view.sliding,true};
            }
        }
        // Slide FOV is transient. Its engine multiplier scales angular mouse
        // response; rendered FOV itself must never be used as a gain ratio.
        if(view.valid&&s.hero.valid&&s.projection.valid&&s.hero.id==slide_hero_&&sniper==slide_sniper_&&view.weapon_zoom==slide_zoom_
            &&motion_clock_ms()<=slide_until_&&!s.replay) {
            const CalibrationRecord* best=nullptr;const Binding* bound=nullptr;
            const float base=view.mouse_sensitivity/view.fov_sensitivity;
            for(const auto& binding:bindings_) {
                if(binding.zoom!=view.weapon_zoom||std::abs(base-binding.base)/binding.base>.02f)continue;
                for(const auto& record:records_)if(record.id==binding.id&&record.hero==s.hero.id&&record.sniper==sniper
                    &&std::abs(record.aspect-s.projection.aspect)/record.aspect<=.02f&&(!bound||binding.observed>bound->observed)) {
                    best=&record;bound=&binding;
                }
            }
            if(best) {
                const float ratio=bound->adjust/view.fov_sensitivity;
                if(std::isfinite(ratio)&&ratio>=.25f&&ratio<=4.f)return {best,best->counts_x*ratio,best->counts_y*ratio,true};
            }
        }
        const auto* record=exact(s,sniper);
        return record?CalibrationMatch{record,record->counts_x,record->counts_y,false}:CalibrationMatch{};
    }
    size_t size()const{return records_.size();}
    const auto& records()const{return records_;}
    bool commit(CalibrationRecord record) {
        record.id=next_;if(!valid(record))return false;
        auto next=records_;
        next.erase(std::remove_if(next.begin(),next.end(),[&](const auto& r){return r.hero==record.hero&&r.sniper==record.sniper
            &&(!r.sensitivity||!record.sensitivity||r.weapon_zoom==record.weapon_zoom)
            &&std::abs(r.horizontal-record.horizontal)/record.horizontal<.02f&&std::abs(r.vertical-record.vertical)/record.vertical<.02f
            &&std::abs(r.aspect-record.aspect)/record.aspect<.02f;}),next.end());
        if(next.size()>=128)next.erase(next.begin());next.push_back(record);
        if(!write_records(next))return false;
        records_=std::move(next);++next_;return true;
    }
    bool verify_scaling(int hero) {
        auto next=records_;const CalibrationRecord* normal=nullptr;
        for(const auto& r:next)if(r.hero==hero&&!r.sniper&&!r.weapon_zoom&&r.sensitivity>0&&r.fov_adjust>0&&(!normal||r.id>normal->id))normal=&r;
        if(!normal)return false;
        auto agrees=[&](const CalibrationRecord& r) {
            auto close=[](float a,float b){return a*b>0&&std::abs(a-b)<=.1f*std::max(std::abs(a),std::abs(b));};
            return r.sensitivity>0&&r.fov_adjust>0&&close(r.counts_x*r.sensitivity,normal->counts_x*normal->sensitivity)
                &&close(r.counts_y*r.sensitivity,normal->counts_y*normal->sensitivity);
        };
        bool confirmed=false;
        for(const auto& r:next)if(r.hero==hero&&r.id!=normal->id&&(!calibration_view(r,view_snapshot(*normal),normal->sniper)||r.weapon_zoom!=normal->weapon_zoom||r.sniper!=normal->sniper)&&agrees(r))confirmed=true;
        for(auto& r:next)if(r.hero==hero)r.scaling_verified=confirmed&&agrees(r);
        if(!write_records(next))return false;records_=std::move(next);return confirmed;
    }
private:
    static Snapshot view_snapshot(const CalibrationRecord& r){Snapshot s;s.hero.valid=true;s.hero.id=r.hero;s.projection={true,r.horizontal,r.vertical,r.aspect};return s;}
};
class CalibrationMeasurement {
    struct PulseObservation {int axis{},pair{},counts{};float degrees{},gain{},return_error{};double elapsed_ms{};};
    enum class Phase { Idle, Countdown, Armed, Writing, Settling } phase_{};
    CalibrationRecord record_{};CameraBasis original_{},pair_origin_{},before_{},previous_{};
    Vec3 player_origin_{};bool root_valid_{},hold_seen_{};
    double started_{},phase_time_{},last_projection_{},projection_stable_since_{},notice_until_{};uint64_t before_sent_{};
    ProjectionInfo previous_view_{};
    int axis_{},pair_{-1},magnitude_{4};bool negative_{};unsigned stable_{};
    std::array<std::array<float,6>,2> gains_{};float positive_gain_{};
    uint64_t session_{};CalibrationMouseWatch* mouse_{};
    std::vector<PulseObservation> observations_;double settle_ms_{};float last_change_{};std::array<long,2> raw_pending_{};
    bool pulse(MakcuInput& input,const Snapshot& s,HWND game,double now) {
        if(!camera_basis(s.matrix,before_))return false;
        if(!negative_)pair_origin_=before_;
        previous_=before_;stable_=0;before_sent_=input.motion_sent();phase_time_=now;last_projection_=s.projection_ms;
        settle_ms_=last_change_=0;raw_pending_={};
        const int count=negative_?-magnitude_:magnitude_;
        const int x=axis_==0?count:0,y=axis_==1?count:0;
        if(mouse_&&!mouse_->expect(x,y))return false;
        if(!(session_?input.calibration_move(game,x,y,session_):input.move(game,x,y,FocusHoldButton::SideButtons)))return false;
        phase_=Phase::Writing;return true;
    }
public:
    std::string status{"Calibration required"};
    bool saved{};
    bool active()const{return phase_!=Phase::Idle;}
    bool notice(double now)const{return active()||now<notice_until_;}
    void write_diagnostics(std::ostream& out)const {
        constexpr const char* names[]={"idle","countdown","armed","writing","settling"};
        out<<"{\"phase\":\""<<names[int(phase_)]<<"\",\"axis\":"<<axis_<<",\"pair\":"<<pair_
            <<",\"counts\":"<<(negative_?-magnitude_:magnitude_)<<",\"stable_samples\":"<<stable_
            <<",\"settle_ms\":"<<settle_ms_<<",\"last_change_degrees\":"<<last_change_
            <<",\"raw_pending\":["<<raw_pending_[0]<<','<<raw_pending_[1]<<"],\"pulses\":[";
        for(size_t i=0;i<observations_.size();++i) {
            const auto& p=observations_[i];if(i)out<<',';
            out<<"{\"axis\":"<<p.axis<<",\"pair\":"<<p.pair<<",\"counts\":"<<p.counts<<",\"degrees\":"<<p.degrees
                <<",\"gain\":"<<p.gain<<",\"return_error\":"<<p.return_error<<",\"elapsed_ms\":"<<p.elapsed_ms<<'}';
        }out<<"]}";
    }
    void cancel(MakcuInput& input,const char* reason="Calibration cancelled") {input.cancel();phase_=Phase::Idle;status=reason;notice_until_=motion_clock_ms()+5000;}
    bool start(MakcuInput& input,const Snapshot& s,bool sniper,uint64_t session=0,CalibrationMouseWatch* mouse=nullptr) {
        if(active())return false;
        if(s.view_input.valid&&s.view_input.sliding){status="Calibrate a stationary view / sliding reuses its calibration";return false;}
        if(!input.ready()||!s.practice||s.replay||!s.hero.valid||!s.camera_valid||!s.projection.valid||!s.local_alive) {
            status="Load a living hero in sandbox to calibrate";return false;
        }
        input.cancel();record_={0,s.hero.id,sniper,s.projection.horizontal_fov,s.projection.vertical_fov,s.projection.aspect};
        session_=session;mouse_=mouse;saved=false;observations_.clear();settle_ms_=last_change_=0;raw_pending_={};
        phase_=session?Phase::Armed:Phase::Countdown;started_=phase_time_=motion_clock_ms();notice_until_=0;axis_=0;pair_=-1;magnitude_=4;negative_=false;gains_={};
        root_valid_=hold_seen_=false;previous_view_=s.projection;projection_stable_since_=started_;
        status="Starting calibration / choose your view, hold a side button and keep still";return true;
    }
    void update(MakcuInput& input,CalibrationStore& store,const Snapshot& s,HWND game,bool menu,bool sniper,double now) {
        if(!active())return;
        if(menu||!game||GetForegroundWindow()!=game||IsIconic(game)||(GetAsyncKeyState(VK_ESCAPE)&0x8000)) {cancel(input);return;}
        const char* failure=!input.ready()?"Calibration stopped / input unavailable"
            :!s.practice||s.replay?"Calibration stopped / sandbox data unavailable"
            :!s.local_alive?"Calibration stopped / local player data unavailable"
            :!s.camera_valid||!s.projection.valid?"Calibration stopped / invalid camera data"
            :!s.time||GetTickCount64()<s.time||GetTickCount64()-s.time>50||s.projection_ms<=0||now-s.projection_ms>50?"Calibration stopped / camera data older than 50 ms"
            :!s.hero.valid?"Calibration stopped / hero data unavailable"
            :s.view_input.valid&&s.view_input.sliding?"Calibration stopped / calibrate stationary; slides reuse that view"
            :record_.hero!=s.hero.id||record_.sniper!=sniper?"Calibration stopped / hero or Assassinate state changed"
            :(phase_==Phase::Writing||phase_==Phase::Settling)&&!calibration_view(record_,s,sniper)?"Calibration stopped / zoom changed during measurement":nullptr;
        if(failure){cancel(input,failure);return;}
        if(now-started_>30000){cancel(input,"Calibration timed out");return;}
        const bool held=session_?input.calibration_live(session_):focus_button_held(FocusHoldButton::SideButtons);
        if(held)hold_seen_=true;
        else if(hold_seen_){cancel(input,"Calibration cancelled / side button released");return;}
        auto changed=[](float a,float b){return !std::isfinite(a)||a<=0||std::abs(a-b)/a>.002f;};
        if(changed(previous_view_.horizontal_fov,s.projection.horizontal_fov)||changed(previous_view_.vertical_fov,s.projection.vertical_fov)||changed(previous_view_.aspect,s.projection.aspect)) {
            if(phase_==Phase::Writing||phase_==Phase::Settling){cancel(input,"Calibration stopped / projection changed");return;}
            projection_stable_since_=now;previous_view_=s.projection;
        }
        if(phase_==Phase::Countdown) {
            const int remaining=3-int((now-started_)/1000);status="Calibration in "+std::to_string(std::max(remaining,0))+" / choose view, hold a side button and keep still";
            if(now-started_<3000)return;
            phase_=Phase::Armed;phase_time_=now;
        }
        CameraBasis current;if(!camera_basis(s.matrix,current)){cancel(input,"Invalid camera view");return;}
        if(phase_==Phase::Armed) {
            status="Hold either side button in the view to calibrate / keep mouse and player still";
            if(!held)return;
            if(now-projection_stable_since_<250){status="Waiting for a stable camera projection";return;}
            if(!s.local_motion.position_valid){status="Waiting for player position";return;}
            // Closing the menu may release weapon zoom. Let the user enter the
            // intended view during the countdown, then lock its stable render
            // projection before any measurement pulse is submitted.
            record_.horizontal=s.projection.horizontal_fov;record_.vertical=s.projection.vertical_fov;record_.aspect=s.projection.aspect;
            if(s.view_input.valid){record_.sensitivity=s.view_input.mouse_sensitivity;record_.fov_adjust=s.view_input.fov_sensitivity;record_.weapon_zoom=s.view_input.weapon_zoom;}
            original_=current;player_origin_=s.local_motion.root;root_valid_=true;
            if(!pulse(input,s,game,now))cancel(input,"Could not submit calibration movement");return;
        }
        if(!held){cancel(input,"Calibration cancelled / side button released");return;}
        if(!root_valid_||!s.local_motion.position_valid||dot(sub(s.local_motion.root,player_origin_),sub(s.local_motion.root,player_origin_))>1.f) {
            cancel(input,"Calibration stopped / player moved");return;
        }
        if(direction_error(before_,current.forward).distance>3.f){cancel(input,"Calibration stopped / camera moved too far");return;}
        if(phase_==Phase::Writing) {
            if(input.motion_sent()>before_sent_){phase_=Phase::Settling;phase_time_=now;}
            else if(now-phase_time_>300)cancel(input,"Calibration movement was not sent");
            return;
        }
        status=std::string("Calibrating ")+(axis_?"vertical":"horizontal")+" / "+(pair_<0?"measuring response":"pair "+std::to_string(pair_+1)+" of 3");
        settle_ms_=now-phase_time_;raw_pending_=mouse_?mouse_->remaining():std::array<long,2>{};
        if(s.projection_ms!=last_projection_) {
            const float change=direction_error(previous_,current.forward).distance;
            last_change_=change;
            stable_=change<.003f?stable_+1:0;previous_=current;last_projection_=s.projection_ms;
        }
        if(now-phase_time_>1000){cancel(input,"Calibration stopped / camera did not settle");return;}
        if(now-phase_time_<125||stable_<4||(mouse_&&!mouse_->complete()))return;
        const auto movement=view_rotation_error(before_,current.forward);
        const float delta=axis_?movement.y:movement.x;
        const float count=float(negative_?-magnitude_:magnitude_);const float gain=delta/count;
        if(!std::isfinite(gain)){cancel(input,"Invalid calibration response");return;}
        if(observations_.size()<64)observations_.push_back({axis_,pair_,int(count),delta,gain,negative_?direction_error(pair_origin_,current.forward).distance:0.f,settle_ms_});
        if(!negative_) {
            positive_gain_=gain;if(pair_>=0)gains_[axis_][size_t(pair_)*2]=gain;
            negative_=true;if(!pulse(input,s,game,now))cancel(input,"Could not return camera");return;
        }
        if(direction_error(pair_origin_,current.forward).distance>=.1f){cancel(input,"Calibration failed / camera did not return accurately");return;}
        if(pair_<0&&std::abs(delta)<.015f&&magnitude_<128) {
            magnitude_*=2;negative_=false;if(!pulse(input,s,game,now))cancel(input,"Could not submit calibration movement");return;
        }
        const float mean=(positive_gain_+gain)*.5f;
        if(std::abs(mean)<1e-7f||positive_gain_*gain<=0||std::abs(positive_gain_-gain)>.1f*std::abs(mean)) {
            cancel(input,"Calibration failed / movement response was inconsistent");return;
        }
        if(pair_>=0)gains_[axis_][size_t(pair_)*2+1]=gain;
        ++pair_;negative_=false;
        if(pair_==3) {
            auto sorted=gains_[axis_];std::sort(sorted.begin(),sorted.end());const float median=(sorted[2]+sorted[3])*.5f;
            for(float v:sorted)if(std::abs(v-median)>.1f*std::abs(median)){cancel(input,"Calibration failed / repeated response differed");return;}
            (axis_?record_.counts_y:record_.counts_x)=1.f/median;
            if(++axis_==2) {
                if(direction_error(original_,current.forward).distance>=.1f){cancel(input,"Calibration failed / final return error");return;}
                input.cancel_motion();phase_=Phase::Idle;notice_until_=now+5000;saved=store.commit(record_);status=saved?"Calibration saved for this hero and view":"Calibration measured but could not be saved";return;
            }
            pair_=-1;magnitude_=4;
        }else magnitude_=std::clamp(int(std::lround(.5f/std::abs(mean))),1,128);
        if(!pulse(input,s,game,now))cancel(input,"Could not submit calibration movement");
    }
};
class FocusCalibrator {
    enum class Phase {Idle,Countdown,Switching,Measuring,Restoring,Cleanup} phase_{};
    CalibrationMeasurement measurement_;CalibrationMouseWatch* mouse_{};
    CalibrationBindings bindings_;uint64_t session_{},control_{},release_control_{};
    CalibrationBinding tapped_;double tap_release_{},control_time_{},started_{},phase_time_{},stable_since_{},notice_until_{};
    ProjectionInfo previous_view_{};Vec3 player_origin_{};CameraBasis stationary_{};
    int hero_{},stage_{},views_hero_{};bool issued_{},zoom_owned_{},root_locked_{},automatic_{},leaving_ultimate_{},leaving_zoom_{};
    std::string final_status_;
    static bool projection_changed(const ProjectionInfo& a,const ProjectionInfo& b) {
        auto changed=[](float x,float y){return x<=0||!std::isfinite(y)||std::abs(x-y)/x>.002f;};
        return changed(a.horizontal_fov,b.horizontal_fov)||changed(a.vertical_fov,b.vertical_fov)||changed(a.aspect,b.aspect);
    }
    static const char* stage_name(int stage){return stage==0?"Normal":stage==1?"Weapon zoom":"Assassinate";}
    bool tap(MakcuInput& input,const CalibrationBinding& binding,double now) {
        if(!binding||tapped_)return false;tapped_=binding;tap_release_=now+80;
        control_time_=now;control_=input.calibration_control(session_,binding,true);return control_!=0;
    }
    bool controls(MakcuInput& input,double now) {
        if(control_&&input.control_sent()<control_) {
            if(now-control_time_>500){cancel(input,"Calibration stopped / view input was not sent");return false;}return false;
        }
        if(tapped_&&now>=tap_release_) {
            control_time_=now;release_control_=input.calibration_control(session_,tapped_,false);tapped_={};
            if(!release_control_){cancel(input,"Calibration stopped / could not release view input");return false;}
        }
        if(release_control_&&input.control_sent()<release_control_&&now-control_time_>500){cancel(input,"Calibration stopped / view input release timed out");return false;}
        return !tapped_&&(!release_control_||input.control_sent()>=release_control_);
    }
    bool release_zoom(MakcuInput& input) {
        if(!zoom_owned_)return true;control_time_=motion_clock_ms();release_control_=input.calibration_control(session_,bindings_.zoom,false);zoom_owned_=false;return release_control_!=0;
    }
    void next(MakcuInput& input,const Snapshot& s,double now) {
        if(!release_zoom(input)){cancel(input,"Could not release weapon zoom");return;}
        ++stage_;issued_=leaving_ultimate_=leaving_zoom_=false;control_=0;phase_time_=stable_since_=now;previous_view_=s.projection;
        if(stage_>=3){phase_=Phase::Restoring;status="Returning to normal view";}
        else phase_=Phase::Switching;
    }
    void finish(MakcuInput& input,double now,const std::string& message) {
        input.end_calibration();if(mouse_)mouse_->stop();session_=0;phase_=Phase::Cleanup;final_status_=message;
        status=input.calibration_cleanup()?"Releasing calibration inputs":message;notice_until_=now+7000;
    }
public:
    std::string status{"Calibration required"};
    std::array<std::string,3> views{"Not measured","Not measured","Not measured"};
    bool sensitivity_verified{};
    bool active()const{return phase_!=Phase::Idle||measurement_.active();}
    bool automatic()const{return automatic_&&active();}
    bool notice(double now)const{return active()||now<notice_until_;}
    const CalibrationBindings& bindings()const{return bindings_;}
    void write_measurement_diagnostics(std::ostream& out)const{measurement_.write_diagnostics(out);}
    void observe_records(const CalibrationStore& store,const Snapshot& s) {
        if(active()||!s.hero.valid||views_hero_==s.hero.id)return;
        views_hero_=s.hero.id;sensitivity_verified=false;views={"Not measured","Not measured",vindicta(s.hero)?"Not measured":"Not applicable"};
        for(const auto& r:store.records())if(r.hero==s.hero.id&&r.sensitivity>0) {
            views[r.sniper?2:r.weapon_zoom?1:0]="Saved";sensitivity_verified|=r.scaling_verified;
        }
        bindings_=find_calibration_bindings(s.hero.token);
    }
    void cancel(MakcuInput& input,const char* reason="Calibration cancelled") {
        const std::string message=reason;
        if(measurement_.active())measurement_.cancel(input,message.c_str());
        if(session_)input.end_calibration();session_=0;if(mouse_)mouse_->stop();zoom_owned_=false;tapped_={};
        phase_=input.calibration_cleanup()?Phase::Cleanup:Phase::Idle;final_status_=message;
        status=phase_==Phase::Cleanup?message+" / waiting for input release":message;notice_until_=motion_clock_ms()+7000;
    }
    bool start(MakcuInput& input,const Snapshot& s,bool sniper,HWND game,CalibrationMouseWatch& mouse,bool automatic=true) {
        if(active())return false;
        if(!automatic) {
            automatic_=false;mouse_=nullptr;phase_=Phase::Idle;
            bool ok=measurement_.start(input,s,sniper);status=measurement_.status;return ok;
        }
        if(!input.ready()||!s.practice||s.replay||!s.hero.valid||!s.local_alive||!s.camera_valid||!s.projection.valid||!s.view_input.valid||!game||!mouse.ready()) {
            status="Load a living hero in sandbox with valid input data to auto calibrate";notice_until_=motion_clock_ms()+7000;return false;
        }
        input.cancel();automatic_=true;mouse_=&mouse;bindings_=find_calibration_bindings(s.hero.token);
        hero_=views_hero_=s.hero.id;stage_=0;issued_=zoom_owned_=root_locked_=leaving_ultimate_=leaving_zoom_=sensitivity_verified=false;
        session_=control_=release_control_=0;tapped_={};views={"Waiting","Waiting",vindicta(s.hero)?"Waiting":"Not applicable"};
        started_=phase_time_=stable_since_=motion_clock_ms();previous_view_=s.projection;camera_basis(s.matrix,stationary_);
        player_origin_=s.local_motion.root;phase_=Phase::Countdown;status="Auto calibration in 3 / release controls and keep still";return true;
    }
    void update(MakcuInput& input,CalibrationStore& store,const Snapshot& s,HWND game,bool menu,bool sniper,double now) {
        if(phase_==Phase::Cleanup){if(!input.calibration_cleanup()){phase_=Phase::Idle;status=final_status_;}return;}
        if(!automatic_) {
            if(measurement_.active()){measurement_.update(input,store,s,game,menu,sniper,now);status=measurement_.status;notice_until_=now+7000;if(measurement_.saved)views_hero_=0;}return;
        }
        if(phase_==Phase::Idle)return;
        const char* failure=menu?"Calibration cancelled / menu opened"
            :!game||GetForegroundWindow()!=game||IsIconic(game)?"Calibration cancelled / game focus lost"
            :(GetAsyncKeyState(VK_ESCAPE)&0x8000)?"Calibration cancelled / Escape"
            :!input.ready()||input.calibration_error()?"Calibration stopped / input unavailable"
            :!s.practice||s.replay?"Calibration stopped / sandbox data unavailable"
            :!s.hero.valid||s.hero.id!=hero_?"Calibration stopped / hero changed"
            :!s.local_alive||!s.local_motion.position_valid?"Calibration stopped / player data unavailable"
            :!s.camera_valid||!s.projection.valid||!s.view_input.valid?"Calibration stopped / camera or sensitivity unavailable"
            :!s.time||GetTickCount64()<s.time||GetTickCount64()-s.time>50||s.projection_ms<=0||now-s.projection_ms>50?"Calibration stopped / stale camera data"
            :s.view_input.sliding?"Calibration stopped / player is sliding"
            :now-started_>60000?"Auto calibration timed out":nullptr;
        if(failure){cancel(input,failure);return;}
        CameraBasis current;if(!camera_basis(s.matrix,current)){cancel(input,"Invalid calibration camera");return;}
        if(phase_==Phase::Countdown) {
            const bool buttons=((GetAsyncKeyState(VK_LBUTTON)|GetAsyncKeyState(VK_RBUTTON)|GetAsyncKeyState(VK_MBUTTON)|GetAsyncKeyState(VK_XBUTTON1)|GetAsyncKeyState(VK_XBUTTON2))&0x8000)!=0;
            const bool key=(bindings_.zoom&&GetAsyncKeyState(bindings_.zoom.vk)&0x8000)||(bindings_.ultimate&&GetAsyncKeyState(bindings_.ultimate.vk)&0x8000);
            const bool moving=dot(sub(s.local_motion.root,player_origin_),sub(s.local_motion.root,player_origin_))>.01f||direction_error(stationary_,current.forward).distance>.01f;
            if(moving||buttons||key){stable_since_=now;player_origin_=s.local_motion.root;stationary_=current;}
            if(now-started_<3000){status="Auto calibration in "+std::to_string(std::max(0,3-int((now-started_)/1000)))+" / release controls and keep still";return;}
            if(now-stable_since_<500){status="Waiting for you to release controls and stay still";return;}
            session_=input.begin_calibration(game);if(!session_){cancel(input,"Could not start calibration input session");return;}
            mouse_->start();root_locked_=true;player_origin_=s.local_motion.root;phase_=Phase::Switching;phase_time_=stable_since_=now;return;
        }
        if(!input.calibration_live(session_)){cancel(input,"Calibration stopped / input session expired");return;}
        input.keep_calibration(session_);
        if(mouse_->unexpected()){cancel(input,"Calibration cancelled / unexpected mouse movement");return;}
        const bool root_moved=dot(sub(s.local_motion.root,player_origin_),sub(s.local_motion.root,player_origin_))>1.f;
        if(root_locked_&&root_moved){cancel(input,"Calibration stopped / player moved");return;}
        if(phase_==Phase::Measuring) {
            measurement_.update(input,store,s,game,menu,sniper,now);status=std::string(stage_name(stage_))+" / "+measurement_.status;
            if(!measurement_.active()) {
                if(!measurement_.saved){cancel(input,status.c_str());return;}
                views[size_t(stage_)]="Saved";next(input,s,now);
            }return;
        }
        if(!controls(input,now))return;
        if(leaving_ultimate_&&!sniper){leaving_ultimate_=issued_=false;control_=0;stable_since_=now;}
        if(leaving_zoom_&&!s.view_input.weapon_zoom){leaving_zoom_=issued_=false;control_=0;stable_since_=now;}
        if(projection_changed(previous_view_,s.projection)){previous_view_=s.projection;stable_since_=now;}
        const bool restoring=phase_==Phase::Restoring;const int wanted=restoring?0:stage_;
        status=restoring?"Returning to normal view":std::string("Preparing ")+stage_name(stage_);
        if(!restoring&&wanted==1&&!bindings_.zoom){views[1]="Pending / zoom binding unavailable";next(input,s,now);return;}
        if(!restoring&&wanted==2&&(!vindicta(s.hero)||!s.hero.sniper_present||!s.hero.sniper_valid||!bindings_.ultimate)) {
            views[2]=vindicta(s.hero)?"Pending / ability or binding unavailable":"Not applicable";next(input,s,now);return;
        }
        const bool matched=wanted==2?sniper:!sniper&&(s.view_input.weapon_zoom==(wanted==1));
        if(matched&&now-stable_since_>=350&&now-phase_time_>=350) {
            if(restoring) {
                sensitivity_verified=store.verify_scaling(hero_);
                std::string summary="Calibration saved / Normal: "+views[0]+" / Zoom: "+views[1];
                if(vindicta(s.hero))summary+=" / Assassinate: "+views[2];
                finish(input,now,summary);return;
            }
            if(!measurement_.start(input,s,sniper,session_,mouse_)){cancel(input,measurement_.status.c_str());return;}
            phase_=Phase::Measuring;return;
        }
        if(!issued_) {
            issued_=true;phase_time_=now;stable_since_=now;
            if(sniper&&wanted!=2) {
                // Assassinate toggles with its Ability4 action. Generic
                // CancelAbility may be Space and does not exit this scope.
                if(!tap(input,bindings_.ultimate,now)){cancel(input,"Cannot leave Assassinate / ultimate binding unavailable");return;}
                leaving_ultimate_=true;
            }else if(wanted==0&&s.view_input.weapon_zoom) {
                // Handles toggle zoom and a view that remained zoomed after
                // the menu stole focus, without leaving a synthetic hold.
                if(!tap(input,bindings_.zoom,now)){cancel(input,"Cannot leave weapon zoom / binding unavailable");return;}
                leaving_zoom_=true;
            }else if(wanted==1&&!sniper) {
                control_time_=now;control_=input.calibration_control(session_,bindings_.zoom,true);zoom_owned_=control_!=0;
                if(!control_){cancel(input,"Could not enter weapon zoom");return;}
            }else if(wanted==2&&!sniper) {
                if(s.view_input.weapon_zoom) {
                    if(zoom_owned_){if(!release_zoom(input)){cancel(input,"Could not release zoom");return;}}
                    else if(!tap(input,bindings_.zoom,now)){views[2]="Pending / could not leave weapon zoom";next(input,s,now);return;}
                    leaving_zoom_=true;
                }
                else if(!tap(input,bindings_.ultimate,now)){views[2]="Pending / ultimate input unavailable";next(input,s,now);return;}
            }
        }
        if(now-phase_time_>4000) {
            if(restoring){sensitivity_verified=store.verify_scaling(hero_);finish(input,now,"Calibration saved / view could not return automatically");}
            else if(stage_==0)cancel(input,"Could not reach normal view / check game bindings");
            else {views[size_t(stage_)]=std::string("Pending / ")+stage_name(stage_)+" unavailable";next(input,s,now);}
        }
    }
};
}
