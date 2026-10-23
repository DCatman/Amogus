#include <httplib.h>
#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include <cstdlib>
#include <cstdio>
#include <ctime>
#include <chrono>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <set>
#include <map>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <random>
#include <thread>
#include <atomic>
#include <functional>
#ifdef _WIN32
#include <tlhelp32.h>
#include <winreg.h>
#endif

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif

using json = nlohmann::json;
namespace fs = std::filesystem;
namespace {
std::mutex dbMutex;
std::mutex googleSyncMutex;
sqlite3* db = nullptr;
std::string oauthState;
std::string googleLastSync;
std::string googleLastError;
std::atomic<bool> windowsBlockerEnabled{false};
std::atomic<bool> windowsWorkerStop{false};
std::atomic<unsigned long long> windowsBlockedCount{0};
std::string windowsLastAction;
std::mutex windowsStatusMutex;


std::string dataPath() {
#ifdef _WIN32
    PWSTR raw = nullptr;
    if (SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw) == S_OK && raw) {
        fs::path dir(raw);
        CoTaskMemFree(raw);
        dir /= L"Amogus";
        fs::create_directories(dir);
        return (dir / L"data.db").string();
    }
#endif
    fs::path dir = fs::current_path() / "data";
    fs::create_directories(dir);
    return (dir / "data.db").string();
}

bool execSql(const char* sql) {
    char* error = nullptr;
    int rc = sqlite3_exec(db, sql, nullptr, nullptr, &error);
    if (rc != SQLITE_OK) {
        std::cerr << "SQLite error: " << (error ? error : "unknown") << '\n';
        sqlite3_free(error);
        return false;
    }
    return true;
}

json errorJson(const std::string& message) { return json{{"error", message}}; }
void sendJson(httplib::Response& res, const json& body, int status = 200) {
    res.status = status;
    res.set_content(body.dump(), "application/json; charset=utf-8");
    res.set_header("Cache-Control", "no-store");
}

bool ensureColumn(const char* table, const char* column, const char* alterSql) {
    std::string pragma = std::string("PRAGMA table_info(") + table + ");";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, pragma.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return false;
    bool found = false;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* name = sqlite3_column_text(stmt, 1);
        if (name && std::string(reinterpret_cast<const char*>(name)) == column) { found = true; break; }
    }
    sqlite3_finalize(stmt);
    return found || execSql(alterSql);
}

bool initDb() {
    if (sqlite3_open(dataPath().c_str(), &db) != SQLITE_OK) return false;
    sqlite3_busy_timeout(db, 3000);
    return execSql("PRAGMA journal_mode=WAL;") &&
           execSql("PRAGMA foreign_keys=ON;") &&
           execSql("CREATE TABLE IF NOT EXISTS windows_block_rules (id TEXT PRIMARY KEY, rule_type TEXT NOT NULL CHECK(rule_type IN ('process','title','path')), pattern TEXT NOT NULL COLLATE NOCASE, enabled INTEGER NOT NULL DEFAULT 1, created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP, UNIQUE(rule_type,pattern));") &&
           execSql("CREATE TABLE IF NOT EXISTS app_settings (key TEXT PRIMARY KEY, value TEXT NOT NULL);") &&
           execSql("CREATE TABLE IF NOT EXISTS profiles ("
                   "id TEXT PRIMARY KEY, name TEXT NOT NULL, locked INTEGER NOT NULL DEFAULT 0, "
                   "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP, updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);") &&
           execSql("CREATE TABLE IF NOT EXISTS calendar_events (id TEXT PRIMARY KEY, title TEXT NOT NULL, start_at TEXT NOT NULL, end_at TEXT NOT NULL, updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP, google_id TEXT NOT NULL DEFAULT '', google_updated_at TEXT NOT NULL DEFAULT '', synced_hash TEXT NOT NULL DEFAULT '');") &&
           ensureColumn("calendar_events", "google_id", "ALTER TABLE calendar_events ADD COLUMN google_id TEXT NOT NULL DEFAULT '';") &&
           ensureColumn("calendar_events", "google_updated_at", "ALTER TABLE calendar_events ADD COLUMN google_updated_at TEXT NOT NULL DEFAULT '';") &&
           ensureColumn("calendar_events", "synced_hash", "ALTER TABLE calendar_events ADD COLUMN synced_hash TEXT NOT NULL DEFAULT '';") &&
           ensureColumn("calendar_events", "event_type", "ALTER TABLE calendar_events ADD COLUMN event_type TEXT NOT NULL DEFAULT 'lecture';") &&
           execSql("CREATE TABLE IF NOT EXISTS google_deleted_events (google_id TEXT PRIMARY KEY, deleted_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);") &&
           execSql("CREATE TABLE IF NOT EXISTS overdue_tasks (id TEXT PRIMARY KEY, title TEXT NOT NULL, due_at TEXT NOT NULL, notes TEXT NOT NULL DEFAULT '', completed INTEGER NOT NULL DEFAULT 0, priority TEXT NOT NULL DEFAULT 'normal', category TEXT NOT NULL DEFAULT 'Ogólne', created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP, updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);") &&
           execSql("CREATE TABLE IF NOT EXISTS profile_domains (id TEXT PRIMARY KEY, profile_id TEXT NOT NULL, domain TEXT NOT NULL COLLATE NOCASE, created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP, UNIQUE(profile_id,domain), FOREIGN KEY(profile_id) REFERENCES profiles(id) ON DELETE CASCADE);") &&
           execSql("CREATE TABLE IF NOT EXISTS profile_youtube_channels (id TEXT PRIMARY KEY, profile_id TEXT NOT NULL, handle TEXT NOT NULL COLLATE NOCASE, created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP, UNIQUE(profile_id,handle), FOREIGN KEY(profile_id) REFERENCES profiles(id) ON DELETE CASCADE);") &&
           execSql("CREATE TABLE IF NOT EXISTS subjects (id TEXT PRIMARY KEY, name TEXT NOT NULL COLLATE NOCASE UNIQUE, color TEXT NOT NULL DEFAULT '#8aa66a', is_lecture INTEGER NOT NULL DEFAULT 1, notes TEXT NOT NULL DEFAULT '', created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP, updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);") &&
           execSql("CREATE TABLE IF NOT EXISTS subject_sessions (id TEXT PRIMARY KEY, subject_id TEXT NOT NULL, weekday INTEGER NOT NULL CHECK(weekday BETWEEN 0 AND 6), start_time TEXT NOT NULL, end_time TEXT NOT NULL, room TEXT NOT NULL DEFAULT '', session_type TEXT NOT NULL DEFAULT 'Wykład', created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP, FOREIGN KEY(subject_id) REFERENCES subjects(id) ON DELETE CASCADE);") &&
           execSql("CREATE TABLE IF NOT EXISTS focus_sessions (id TEXT PRIMARY KEY, subject_id TEXT, duration_seconds INTEGER NOT NULL CHECK(duration_seconds BETWEEN 60 AND 14400), started_at TEXT NOT NULL, ended_at TEXT NOT NULL, note TEXT NOT NULL DEFAULT '', created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP, FOREIGN KEY(subject_id) REFERENCES subjects(id) ON DELETE SET NULL);") &&
           execSql("CREATE TABLE IF NOT EXISTS study_cards (id TEXT PRIMARY KEY, subject_id TEXT, front TEXT NOT NULL, back TEXT NOT NULL, due_at TEXT NOT NULL, interval_days INTEGER NOT NULL DEFAULT 0, repetitions INTEGER NOT NULL DEFAULT 0, last_reviewed TEXT NOT NULL DEFAULT '', created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP, FOREIGN KEY(subject_id) REFERENCES subjects(id) ON DELETE SET NULL);") &&
           execSql("CREATE TABLE IF NOT EXISTS study_plans (id TEXT PRIMARY KEY, subject_id TEXT, title TEXT NOT NULL, target_date TEXT NOT NULL DEFAULT '', notes TEXT NOT NULL DEFAULT '', created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP, updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP, FOREIGN KEY(subject_id) REFERENCES subjects(id) ON DELETE SET NULL);") &&
           execSql("CREATE TABLE IF NOT EXISTS study_stages (id TEXT PRIMARY KEY, plan_id TEXT NOT NULL, title TEXT NOT NULL, stage_order INTEGER NOT NULL DEFAULT 0, due_at TEXT NOT NULL DEFAULT '', completed INTEGER NOT NULL DEFAULT 0, notes TEXT NOT NULL DEFAULT '', completed_at TEXT NOT NULL DEFAULT '', created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP, FOREIGN KEY(plan_id) REFERENCES study_plans(id) ON DELETE CASCADE);") &&
           execSql("INSERT OR IGNORE INTO profiles(id,name,locked) VALUES('default','Default profile',0);") &&
           ensureColumn("overdue_tasks", "priority", "ALTER TABLE overdue_tasks ADD COLUMN priority TEXT NOT NULL DEFAULT 'normal';") &&
           ensureColumn("overdue_tasks", "category", "ALTER TABLE overdue_tasks ADD COLUMN category TEXT NOT NULL DEFAULT 'Ogólne';");
}

json listProfiles() {
    json out = json::array();
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id,name,locked,created_at,updated_at FROM profiles ORDER BY created_at,id";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return out;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        auto col = [&](int i) { const unsigned char* p = sqlite3_column_text(stmt, i); return p ? std::string(reinterpret_cast<const char*>(p)) : std::string(); };
        out.push_back({{"id", col(0)}, {"name", col(1)}, {"locked", sqlite3_column_int(stmt, 2) != 0}, {"createdAt", col(3)}, {"updatedAt", col(4)}});
    }
    sqlite3_finalize(stmt);
    return out;
}

std::string shellQuote(const std::string& value) {
#ifdef _WIN32
    std::string out = "\"";
    for (char c : value) { if (c == '"') out += "\\\""; else if (c == '%') out += "%%"; else out += c; }
    out += "\""; return out;
#else
    std::string out = "'";
    for (char c : value) { if (c == '\'') out += "'\\''"; else out += c; }
    out += "'"; return out;
#endif
}
std::string urlEncode(const std::string& value) {
    std::ostringstream out; out << std::hex << std::uppercase;
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out << c;
        else if (c == ' ') out << '+';
        else out << '%' << std::setw(2) << std::setfill('0') << (int)c;
    }
    return out.str();
}
std::string randomHex(size_t n=24) {
    static const char* chars="0123456789abcdef"; std::random_device rd; std::string out;
    for(size_t i=0;i<n;i++) out += chars[rd()%16]; return out;
}
fs::path dataDir() { auto p=fs::path(dataPath()).parent_path(); fs::create_directories(p); return p; }
fs::path findCredentials() {
    fs::path base=fs::current_path();
    for(int depth=0;depth<7&&!base.empty();++depth){
        std::vector<fs::path> paths={base/"Library"/"credentials.json",base/"#Library Files"/"credentials.json",base/"credentials.json"};
        for(const auto& p:paths) if(fs::exists(p)&&fs::is_regular_file(p)) return p;
        auto parent=base.parent_path(); if(parent==base)break; base=parent;
    }
    return {};
}
json readJsonFile(const fs::path& path) { std::ifstream f(path); if(!f) return json(); try { json j; f>>j; return j; } catch(...) { return json(); } }
bool writeJsonFile(const fs::path& path,const json& value) { std::ofstream f(path,std::ios::trunc); if(!f)return false; f<<value.dump(2); return !!f; }
struct GoogleCredentials { std::string clientId, clientSecret, authUri="https://accounts.google.com/o/oauth2/v2/auth", tokenUri="https://oauth2.googleapis.com/token"; };
bool loadGoogleCredentials(GoogleCredentials& c) {
    auto path=findCredentials(); if(path.empty()) return false; json j=readJsonFile(path); if(!j.is_object())return false;
    json x=j.value("installed",j.value("web",j)); if(!x.is_object())x=j;
    c.clientId=x.value("client_id",std::string()); c.clientSecret=x.value("client_secret",std::string());
    c.authUri=x.value("auth_uri",c.authUri); c.tokenUri=x.value("token_uri",c.tokenUri);
    return !c.clientId.empty() && !c.clientSecret.empty();
}
fs::path googleTokenPath(){return dataDir()/"google_token.json";}
json loadGoogleToken(){return readJsonFile(googleTokenPath());}
std::string curlRequest(const std::string& method,const std::string& url,const std::vector<std::string>& headers={},const std::string& body="") {
    auto base=dataDir(); auto nonce=randomHex(8); auto input=base/("gcal-in-"+nonce+".json"); auto output=base/("gcal-out-"+nonce+".json"); auto status=base/("gcal-status-"+nonce+".txt");
    if(!body.empty()){std::ofstream f(input,std::ios::binary);f<<body;}
    #ifdef _WIN32
    std::vector<std::string> args={"curl.exe","--silent","--show-error","--max-time","25","--request",method,"--url",url,"--output",output.string(),"--write-out","%{http_code}"};
#else
    std::vector<std::string> args={"curl","--silent","--show-error","--max-time","25","--request",method,"--url",url,"--output",output.string(),"--write-out","%{http_code}"};
#endif
    for(const auto& h:headers){args.push_back("--header");args.push_back(h);} if(!body.empty()){args.push_back("--data-binary");args.push_back("@"+input.string());}
    std::string script;
#ifdef _WIN32
    script="$ErrorActionPreference='Stop'; $a=@("; for(size_t i=1;i<args.size();++i){if(i>1)script+=",";std::string v=args[i];std::string q="'";for(char c:v){if(c=='\'')q+="''";else q+=c;}q+="'";script+=q;} script+="); & curl.exe @a | Out-File -Encoding ascii "+shellQuote(status.string());
    // PowerShell's -EncodedCommand accepts UTF-16LE base64.
    std::string utf16; for(unsigned char c:script){utf16.push_back((char)c);utf16.push_back('\0');}
    static const char* b64="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";std::string enc;unsigned val=0,bits=-6;for(unsigned char c:utf16){val=(val<<8)+c;bits+=8;while(bits>=0){enc.push_back(b64[(val>>bits)&63]);bits-=6;}}if(bits>-6)enc.push_back(b64[((val<<8)>>(bits+8))&63]);while(enc.size()%4)enc.push_back('=');
    std::string cmd="powershell.exe -NoProfile -ExecutionPolicy Bypass -EncodedCommand "+enc;
#else
    for(size_t i=0;i<args.size();++i){if(i)script+=' ';script+=shellQuote(args[i]);} script+=" > "+shellQuote(status.string()); std::string cmd=script;
#endif
    int rc=std::system(cmd.c_str()); std::ifstream out(output,std::ios::binary);std::ostringstream contents;contents<<out.rdbuf();
    std::ifstream sf(status);std::string code;sf>>code;
    std::error_code ec;fs::remove(input,ec);fs::remove(output,ec);fs::remove(status,ec);
    if(rc!=0 && contents.str().empty())return "{\"error\":\"curl execution failed\"}";
    if(contents.str().empty() && !code.empty() && code!="200" && code!="201" && code!="202" && code!="204")return "{\"error\":\"HTTP "+code+"\"}";
    return contents.str();
}
std::string eventHash(const std::string& title,const std::string& start,const std::string& end){return title+"\\n"+start+"\\n"+end;}
std::string normalizeDate(const json& dt) { if(dt.is_string())return dt.get<std::string>(); if(dt.is_object()&&dt.contains("dateTime")&&dt["dateTime"].is_string())return dt["dateTime"].get<std::string>(); if(dt.is_object()&&dt.contains("date")&&dt["date"].is_string())return dt["date"].get<std::string>(); return {}; }
std::string googleEventBody(const std::string& id,const std::string& title,const std::string& start,const std::string& end){json st,en;if(start.size()==10)st={{"date",start}};else st={{"dateTime",start}};if(end.size()==10)en={{"date",end}};else en={{"dateTime",end}};json j={{"summary",title},{"start",st},{"end",en},{"extendedProperties",{{"private",{{"amogusLocalId",id}}}}}};return j.dump();}
bool googleError(const std::string& response){try{auto j=json::parse(response);return j.contains("error");}catch(...){return true;}}
std::string getAccessToken(GoogleCredentials& c,json& token) {
    token=loadGoogleToken(); if(!token.is_object()||!token.contains("refresh_token"))return {};
    std::string body="client_id="+urlEncode(c.clientId)+"&client_secret="+urlEncode(c.clientSecret)+"&refresh_token="+urlEncode(token.value("refresh_token",std::string()))+"&grant_type=refresh_token";
    std::string r=curlRequest("POST",c.tokenUri,{"Content-Type: application/x-www-form-urlencoded"},body);
    json j;try{j=json::parse(r);}catch(...){return {};}
    if(!j.contains("access_token"))return {};
    token["access_token"]=j["access_token"];token["expires_at"]=std::time(nullptr)+j.value("expires_in",3600)-60;writeJsonFile(googleTokenPath(),token);
    return token.value("access_token",std::string());
}
json googleStatus(){GoogleCredentials c;bool configured=loadGoogleCredentials(c);json t=loadGoogleToken();return {{"configured",configured},{"authenticated",t.is_object()&&t.contains("refresh_token")},{"lastSync",googleLastSync},{"lastError",googleLastError},{"credentialsPath",configured?findCredentials().string():std::string((fs::current_path()/"Library"/"credentials.json").string())}};}
}

namespace {
std::string lowerAscii(std::string value) { std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); }); return value; }
bool containsPattern(const std::string& text, const std::string& pattern) {
    const auto t=lowerAscii(text), p=lowerAscii(pattern);
    if (p.empty()) return false;
    if (p.find('*') == std::string::npos) return t.find(p) != std::string::npos;
    std::stringstream ss(p); std::string part; std::size_t pos=0;
    while(std::getline(ss,part,'*')) { if(part.empty()) continue; auto found=t.find(part,pos); if(found==std::string::npos)return false; pos=found+part.size(); }
    return true;
}
struct WinRule { std::string id,type,pattern; bool enabled; };
bool loadWindowsBlockerSetting() { std::lock_guard<std::mutex> lock(dbMutex); sqlite3_stmt*st=nullptr; bool enabled=false; if(sqlite3_prepare_v2(db,"SELECT value FROM app_settings WHERE key='windows_blocker_enabled'",-1,&st,nullptr)==SQLITE_OK && sqlite3_step(st)==SQLITE_ROW){const unsigned char*p=sqlite3_column_text(st,0);enabled=p&&std::string(reinterpret_cast<const char*>(p))=="1";} if(st)sqlite3_finalize(st);return enabled; }
bool saveWindowsBlockerSetting(bool enabled) { std::lock_guard<std::mutex> lock(dbMutex); sqlite3_stmt*st=nullptr; if(sqlite3_prepare_v2(db,"INSERT INTO app_settings(key,value) VALUES('windows_blocker_enabled',?) ON CONFLICT(key) DO UPDATE SET value=excluded.value",-1,&st,nullptr)!=SQLITE_OK)return false;const char*v=enabled?"1":"0";sqlite3_bind_text(st,1,v,-1,SQLITE_TRANSIENT);bool ok=sqlite3_step(st)==SQLITE_DONE;sqlite3_finalize(st);return ok; }
std::vector<WinRule> loadWindowsRules() {
    std::vector<WinRule> out; std::lock_guard<std::mutex> lock(dbMutex); sqlite3_stmt* st=nullptr;
    if(sqlite3_prepare_v2(db,"SELECT id,rule_type,pattern,enabled FROM windows_block_rules WHERE enabled=1",-1,&st,nullptr)!=SQLITE_OK)return out;
    while(sqlite3_step(st)==SQLITE_ROW){auto col=[&](int i){const unsigned char*p=sqlite3_column_text(st,i);return p?std::string(reinterpret_cast<const char*>(p)):std::string();};out.push_back({col(0),col(1),col(2),sqlite3_column_int(st,3)!=0});} sqlite3_finalize(st);return out;
}
#ifdef _WIN32
std::string wideToUtf8(const std::wstring& value) { if(value.empty()) return {}; int n=WideCharToMultiByte(CP_UTF8,0,value.c_str(),-1,nullptr,0,nullptr,nullptr); if(n<=1)return {}; std::string out(static_cast<std::size_t>(n), '\0'); WideCharToMultiByte(CP_UTF8,0,value.c_str(),-1,out.data(),n,nullptr,nullptr); out.resize(static_cast<std::size_t>(n-1)); return out; }
bool protectedProcessName(const std::string& name) {
    static const std::set<std::string> protectedNames={"system","registry","smss.exe","csrss.exe","wininit.exe","services.exe","lsass.exe","winlogon.exe","explorer.exe","dwm.exe","amogus_core.exe","amogus.exe","amogusguard.exe"};
    return protectedNames.count(lowerAscii(name))>0;
}
void recordWindowsAction(const std::string& action) { std::lock_guard<std::mutex> lock(windowsStatusMutex); windowsLastAction=action; ++windowsBlockedCount; }
void terminateIfMatched(DWORD pid,const std::string& exe,const std::string& path,const std::vector<WinRule>& rules) {
    if(pid==0 || pid==GetCurrentProcessId() || protectedProcessName(exe)) return;
    for(const auto&r:rules){if(!r.enabled)continue;bool match=(r.type=="process"&&lowerAscii(exe)==lowerAscii(r.pattern))||(r.type=="path"&&containsPattern(path,r.pattern));if(!match)continue;
      HANDLE h=OpenProcess(PROCESS_TERMINATE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);if(h){if(TerminateProcess(h,0)){recordWindowsAction(exe+" ("+r.type+")");}CloseHandle(h);}return;}
}
BOOL CALLBACK inspectWindow(HWND hwnd, LPARAM param) {
    DWORD pid=0;GetWindowThreadProcessId(hwnd,&pid);if(!pid||!IsWindowVisible(hwnd)||pid==GetCurrentProcessId())return TRUE;
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);if(process){wchar_t imagePath[32768]{};DWORD imageSize=32768;if(QueryFullProcessImageNameW(process,0,imagePath,&imageSize)){std::wstring full(imagePath,imageSize);auto slash=full.find_last_of(L"\\/");std::string exe=wideToUtf8(slash==std::wstring::npos?full:full.substr(slash+1));if(protectedProcessName(exe)){CloseHandle(process);return TRUE;}}CloseHandle(process);}
    wchar_t title[1024]{};int len=GetWindowTextW(hwnd,title,1023);if(len<=0)return TRUE;
    std::wstring wt(title);std::string utf8=wideToUtf8(wt);
    auto* rules=reinterpret_cast<std::vector<WinRule>*>(param);
    for(const auto&r:*rules)if(r.enabled&&r.type=="title"&&containsPattern(utf8,r.pattern)){
      HANDLE h=OpenProcess(PROCESS_TERMINATE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);if(h){if(TerminateProcess(h,0)){recordWindowsAction("okno: "+utf8.substr(0,100));}CloseHandle(h);}break;}
    return TRUE;
}
void windowsBlockerWorker() {
    while(!windowsWorkerStop.load()) {
      if(windowsBlockerEnabled.load()) {
        auto rules=loadWindowsRules();
        HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
        if(snap!=INVALID_HANDLE_VALUE){PROCESSENTRY32W pe{};pe.dwSize=sizeof(pe);if(Process32FirstW(snap,&pe)){do{std::wstring wexe(pe.szExeFile);std::string exe=wideToUtf8(wexe);std::string path;HANDLE ph=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pe.th32ProcessID);if(ph){wchar_t buf[32768];DWORD sz=32768;if(QueryFullProcessImageNameW(ph,0,buf,&sz)){std::wstring wp(buf,sz);path=wideToUtf8(wp);}CloseHandle(ph);}terminateIfMatched(pe.th32ProcessID,exe,path,rules);}while(Process32NextW(snap,&pe));}CloseHandle(snap);}
        EnumWindows(inspectWindow,reinterpret_cast<LPARAM>(&rules));
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(900));
    }
}
std::string currentExePath(){wchar_t path[32768]{};DWORD n=GetModuleFileNameW(nullptr,path,32768);if(!n||n>=32768)return {};std::wstring w(path,n);return wideToUtf8(w);}
bool setWindowsAutostart(bool enabled){HKEY key{};if(RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,KEY_SET_VALUE,&key)!=ERROR_SUCCESS)return false;bool ok=false;if(enabled){auto exe=currentExePath();if(exe.find('"')!=std::string::npos||exe.empty()){RegCloseKey(key);return false;}int n=MultiByteToWideChar(CP_UTF8,0,exe.c_str(),-1,nullptr,0);std::wstring w(n>0?static_cast<std::size_t>(n):0,L' ' );if(n>0){MultiByteToWideChar(CP_UTF8,0,exe.c_str(),-1,w.data(),n);if(!w.empty()&&w.back()==L'\0')w.pop_back();}std::wstring cmd=L"\""+w+L"\"";ok=RegSetValueExW(key,L"AmogusCore",0,REG_SZ,reinterpret_cast<const BYTE*>(cmd.c_str()),static_cast<DWORD>((cmd.size()+1)*sizeof(wchar_t)))==ERROR_SUCCESS;}else{LONG rc=RegDeleteValueW(key,L"AmogusCore");ok=rc==ERROR_SUCCESS||rc==ERROR_FILE_NOT_FOUND;}RegCloseKey(key);return ok;}
bool windowsAutostartEnabled(){HKEY key{};if(RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,KEY_QUERY_VALUE,&key)!=ERROR_SUCCESS)return false;LONG rc=RegQueryValueExW(key,L"AmogusCore",nullptr,nullptr,nullptr,nullptr);RegCloseKey(key);return rc==ERROR_SUCCESS;}
#endif
json windowsStatus(){
#ifdef _WIN32
 std::lock_guard<std::mutex> lock(windowsStatusMutex);return {{"supported",true},{"blockerEnabled",windowsBlockerEnabled.load()},{"autostart",windowsAutostartEnabled()},{"blockedCount",windowsBlockedCount.load()},{"lastAction",windowsLastAction}};
#else
 return {{"supported",false},{"blockerEnabled",false},{"autostart",false},{"blockedCount",0},{"lastAction",""},{"message","Funkcje systemowe są dostępne tylko w Windows."}};
#endif
}
}

int main() {
    if (!initDb()) {
        std::cerr << "Unable to initialize the Amogus database.\n";
        return 2;
    }

    httplib::Server server;
    // Deliberately bind to loopback only: this service is not exposed to the LAN.
    server.set_address_family(AF_INET);
    server.set_keep_alive_max_count(20);
    server.set_payload_max_length(10 * 1024 * 1024);
    server.set_read_timeout(5, 0);
    server.set_write_timeout(5, 0);
    // Reject cross-origin browser requests and unexpected Host headers. This is
    // defense-in-depth, not a substitute for per-run authentication before adding
    // security-sensitive commands.
    server.set_pre_routing_handler([](const httplib::Request& req, httplib::Response& res) {
        auto host = req.get_header_value("Host");
        if (!host.empty() && host != "127.0.0.1:43127") {
            sendJson(res, errorJson("Unexpected Host header"), 403);
            return httplib::Server::HandlerResponse::Handled;
        }
        auto origin = req.get_header_value("Origin");
        const bool extensionReadOnly = origin.rfind("chrome-extension://", 0) == 0 && req.method == "GET" && req.path == "/api/blocking/config";
        const bool oauthLoopbackCallback = req.method == "GET" && req.path == "/";
        if (!origin.empty() && origin != "http://127.0.0.1:5173" && !extensionReadOnly && !oauthLoopbackCallback) {
            sendJson(res, errorJson("Origin not allowed"), 403);
            return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled;
    });

    server.Options("/.*", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "http://127.0.0.1:5173");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
        res.status = 204;
    });
    auto cors = [](httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "http://127.0.0.1:5173");
        res.set_header("Vary", "Origin");
    };

    server.Get("/api/backup", [&](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dbMutex); cors(res);
        json backup = {{"format", "amogus-backup"}, {"version", 1}, {"createdAt", std::chrono::system_clock::now().time_since_epoch().count()}, {"profiles", json::array()}, {"domains", json::array()}, {"youtubeChannels", json::array()}, {"subjects", json::array()}, {"events", json::array()}, {"tasks", json::array()}, {"sessions", json::array()}, {"focusSessions", json::array()}, {"studyCards", json::array()}, {"studyPlans", json::array()}, {"studyStages", json::array()}};
        auto collect = [&](const char* sql, const char* key, int columns, auto makeRow) {
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
            while (sqlite3_step(stmt) == SQLITE_ROW) backup[key].push_back(makeRow(stmt, columns));
            sqlite3_finalize(stmt); return true;
        };
        auto col = [](sqlite3_stmt* stmt, int i) { const unsigned char* p = sqlite3_column_text(stmt, i); return p ? std::string(reinterpret_cast<const char*>(p)) : std::string(); };
        bool ok = collect("SELECT id,name,locked,created_at,updated_at FROM profiles", "profiles", 5, [&](sqlite3_stmt* q,int){return json{{"id",col(q,0)},{"name",col(q,1)},{"locked",sqlite3_column_int(q,2)!=0},{"createdAt",col(q,3)},{"updatedAt",col(q,4)}};}) &&
          collect("SELECT id,profile_id,domain,created_at FROM profile_domains", "domains", 4, [&](sqlite3_stmt* q,int){return json{{"id",col(q,0)},{"profileId",col(q,1)},{"domain",col(q,2)},{"createdAt",col(q,3)}};}) &&
          collect("SELECT id,profile_id,handle,created_at FROM profile_youtube_channels", "youtubeChannels", 4, [&](sqlite3_stmt* q,int){return json{{"id",col(q,0)},{"profileId",col(q,1)},{"handle",col(q,2)},{"createdAt",col(q,3)}};}) &&
          collect("SELECT id,name,color,is_lecture,notes,created_at,updated_at FROM subjects", "subjects", 7, [&](sqlite3_stmt* q,int){return json{{"id",col(q,0)},{"name",col(q,1)},{"color",col(q,2)},{"isLecture",sqlite3_column_int(q,3)!=0},{"notes",col(q,4)},{"createdAt",col(q,5)},{"updatedAt",col(q,6)}};}) &&
          collect("SELECT id,title,start_at,end_at,updated_at FROM calendar_events", "events", 5, [&](sqlite3_stmt* q,int){return json{{"id",col(q,0)},{"title",col(q,1)},{"start",col(q,2)},{"end",col(q,3)},{"updatedAt",col(q,4)}};}) &&
          collect("SELECT id,title,due_at,notes,completed,priority,category,created_at,updated_at FROM overdue_tasks", "tasks", 9, [&](sqlite3_stmt* q,int){return json{{"id",col(q,0)},{"title",col(q,1)},{"dueAt",col(q,2)},{"notes",col(q,3)},{"completed",sqlite3_column_int(q,4)!=0},{"priority",col(q,5)},{"category",col(q,6)},{"createdAt",col(q,7)},{"updatedAt",col(q,8)}};}) &&
          collect("SELECT id,subject_id,weekday,start_time,end_time,room,session_type FROM subject_sessions", "sessions", 7, [&](sqlite3_stmt* q,int){return json{{"id",col(q,0)},{"subjectId",col(q,1)},{"weekday",sqlite3_column_int(q,2)},{"startTime",col(q,3)},{"endTime",col(q,4)},{"room",col(q,5)},{"sessionType",col(q,6)}};}) &&
          collect("SELECT id,COALESCE(subject_id,''),duration_seconds,started_at,ended_at,note FROM focus_sessions ORDER BY started_at DESC", "focusSessions", 6, [&](sqlite3_stmt* q,int){return json{{"id",col(q,0)},{"subjectId",col(q,1)},{"durationSeconds",sqlite3_column_int(q,2)},{"startedAt",col(q,3)},{"endedAt",col(q,4)},{"note",col(q,5)}};}) &&
          collect("SELECT id,COALESCE(subject_id,''),front,back,due_at,interval_days,repetitions,last_reviewed,created_at FROM study_cards", "studyCards", 9, [&](sqlite3_stmt* q,int){return json{{"id",col(q,0)},{"subjectId",col(q,1)},{"front",col(q,2)},{"back",col(q,3)},{"dueAt",col(q,4)},{"intervalDays",sqlite3_column_int(q,5)},{"repetitions",sqlite3_column_int(q,6)},{"lastReviewed",col(q,7)},{"createdAt",col(q,8)}};}) &&
          collect("SELECT id,COALESCE(subject_id,''),title,target_date,notes,created_at,updated_at FROM study_plans", "studyPlans", 7, [&](sqlite3_stmt* q,int){return json{{"id",col(q,0)},{"subjectId",col(q,1)},{"title",col(q,2)},{"targetDate",col(q,3)},{"notes",col(q,4)},{"createdAt",col(q,5)},{"updatedAt",col(q,6)}};}) &&
          collect("SELECT id,plan_id,title,stage_order,due_at,completed,notes,completed_at,created_at FROM study_stages", "studyStages", 9, [&](sqlite3_stmt* q,int){return json{{"id",col(q,0)},{"planId",col(q,1)},{"title",col(q,2)},{"stageOrder",sqlite3_column_int(q,3)},{"dueAt",col(q,4)},{"completed",sqlite3_column_int(q,5)!=0},{"notes",col(q,6)},{"completedAt",col(q,7)},{"createdAt",col(q,8)}};});
        if (!ok) { sendJson(res, errorJson("Nie udało się przygotować kopii zapasowej."), 500); return; }
        res.set_header("Content-Disposition", "attachment; filename=amogus-backup.json"); sendJson(res, backup);
    });

    // Restore a previously exported backup atomically. Validation happens before
    // destructive changes; any failed insert rolls the whole transaction back.
    server.Post("/api/backup/restore", [&](const httplib::Request& req, httplib::Response& res) {
        cors(res);
        json backup;
        try { backup = json::parse(req.body); }
        catch (...) { sendJson(res, errorJson("Plik kopii nie zawiera poprawnego JSON."), 400); return; }
        if (!backup.is_object() || !backup.contains("format") || !backup["format"].is_string() || backup["format"] != "amogus-backup") {
            sendJson(res, errorJson("To nie jest kopia zapasowa Amogus."), 400); return;
        }
        if (!backup.contains("sessions")) backup["sessions"] = json::array();
        if (!backup.contains("focusSessions")) backup["focusSessions"] = json::array();
        if (!backup.contains("studyCards")) backup["studyCards"] = json::array();
        if (!backup.contains("studyPlans")) backup["studyPlans"] = json::array();
        if (!backup.contains("studyStages")) backup["studyStages"] = json::array();
        if (!backup.contains("youtubeChannels")) backup["youtubeChannels"] = json::array();
        if (!backup["youtubeChannels"].is_array() || backup["youtubeChannels"].size() > 100000) { sendJson(res,errorJson("Nieprawidłowa sekcja kopii: youtubeChannels"),400); return; }
        if (!backup["sessions"].is_array() || backup["sessions"].size() > 100000) { sendJson(res,errorJson("Nieprawidłowa sekcja kopii: sessions"),400); return; }
        if (!backup["focusSessions"].is_array() || backup["focusSessions"].size() > 100000) { sendJson(res,errorJson("Nieprawidłowa sekcja kopii: focusSessions"),400); return; }
        if (!backup["studyCards"].is_array() || backup["studyCards"].size() > 100000) { sendJson(res,errorJson("Nieprawidłowa sekcja kopii: studyCards"),400); return; }
        if (!backup["studyPlans"].is_array() || backup["studyPlans"].size() > 100000) { sendJson(res,errorJson("Nieprawidłowa sekcja kopii: studyPlans"),400); return; }
        if (!backup["studyStages"].is_array() || backup["studyStages"].size() > 500000) { sendJson(res,errorJson("Nieprawidłowa sekcja kopii: studyStages"),400); return; }
        const char* arrays[] = {"profiles", "domains", "subjects", "events", "tasks"};
        for (const char* key : arrays) {
            if (!backup.contains(key) || !backup[key].is_array() || backup[key].size() > 100000) {
                sendJson(res, errorJson(std::string("Nieprawidłowa sekcja kopii: ") + key), 400); return;
            }
        }
        auto stringField = [](const json& row, const char* key, size_t maxLen, bool allowEmpty = false) {
            return row.is_object() && row.contains(key) && row[key].is_string() &&
                   row[key].get_ref<const std::string&>().size() <= maxLen &&
                   (allowEmpty || !row[key].get_ref<const std::string&>().empty());
        };
        std::set<std::string> profileIds;
        for (const auto& row : backup["profiles"]) {
            if (!stringField(row,"id",120) || !stringField(row,"name",200) || (row.contains("locked") && !row["locked"].is_boolean())) { sendJson(res,errorJson("Nieprawidłowy profil w kopii."),400); return; }
            if (!profileIds.insert(row["id"].get<std::string>()).second) { sendJson(res,errorJson("Duplikat identyfikatora profilu w kopii."),400); return; }
        }
        if (!profileIds.count("default")) { sendJson(res,errorJson("Kopia musi zawierać chroniony profil domyślny."),400); return; }
        for (const auto& row : backup["domains"]) {
            if (!stringField(row,"id",120) || !stringField(row,"profileId",120) || !stringField(row,"domain",253) || !profileIds.count(row.value("profileId",std::string()))) { sendJson(res,errorJson("Nieprawidłowa reguła domeny w kopii."),400); return; }
        }
        for (const auto& row : backup["youtubeChannels"]) {
            if (!stringField(row,"id",120) || !stringField(row,"profileId",120) || !stringField(row,"handle",101) || !profileIds.count(row.value("profileId",std::string()))) { sendJson(res,errorJson("Nieprawidłowy kanał YouTube w kopii."),400); return; }
            std::string handle = row.value("handle",std::string()); if (!handle.empty() && handle.front()=='@') handle.erase(handle.begin());
            if (handle.size()<3 || handle.size()>30 || !std::all_of(handle.begin(),handle.end(),[](unsigned char c){return std::isalnum(c)||c=='_'||c=='-'||c=='.';})) { sendJson(res,errorJson("Uchwyt kanału YouTube musi mieć 3–30 znaków: litery, cyfry, kropki, podkreślenia lub myślniki."),400); return; }
        }
        for (const auto& row : backup["subjects"]) {
            if (!stringField(row,"id",120) || !stringField(row,"name",120) || !stringField(row,"color",7) || row["color"].get<std::string>().size()!=7 || row["color"].get<std::string>()[0]!='#' || (row.contains("isLecture") && !row["isLecture"].is_boolean()) || (row.contains("notes") && !row["notes"].is_string())) { sendJson(res,errorJson("Nieprawidłowy przedmiot w kopii."),400); return; }
            for (size_t i=1;i<7;++i) if (!std::isxdigit(static_cast<unsigned char>(row["color"].get<std::string>()[i]))) { sendJson(res,errorJson("Nieprawidłowy kolor przedmiotu w kopii."),400); return; }
        }
        for (const auto& row : backup["events"]) if (!stringField(row,"id",120) || !stringField(row,"title",200) || !stringField(row,"start",40) || !stringField(row,"end",40)) { sendJson(res,errorJson("Nieprawidłowe wydarzenie w kopii."),400); return; }
        for (const auto& row : backup["tasks"]) if (!stringField(row,"id",120) || !stringField(row,"title",200) || !stringField(row,"dueAt",40) || !stringField(row,"notes",2000,true) || (row.contains("completed") && !row["completed"].is_boolean()) || (row.contains("priority") && (!row["priority"].is_string() || (row["priority"]!="low" && row["priority"]!="normal" && row["priority"]!="high" && row["priority"]!="urgent"))) || (row.contains("category") && (!row["category"].is_string() || row["category"].get<std::string>().size()>80))) { sendJson(res,errorJson("Nieprawidłowe zadanie w kopii."),400); return; }
        std::set<std::string> subjectIds; for (const auto& row : backup["subjects"]) subjectIds.insert(row.value("id", std::string()));
        for (const auto& row : backup["sessions"]) if (!stringField(row,"id",120) || !stringField(row,"subjectId",120) || !subjectIds.count(row.value("subjectId",std::string())) || !row.contains("weekday") || !row["weekday"].is_number_integer() || row["weekday"].get<int>()<0 || row["weekday"].get<int>()>6 || !stringField(row,"startTime",5) || !stringField(row,"endTime",5) || !stringField(row,"room",120,true) || !stringField(row,"sessionType",40)) { sendJson(res,errorJson("Nieprawidłowe zajęcia w kopii."),400); return; }
        for (const auto& row : backup["focusSessions"]) if (!stringField(row,"id",120) || !stringField(row,"startedAt",40) || !stringField(row,"endedAt",40) || !stringField(row,"note",500,true) || !row.contains("durationSeconds") || !row["durationSeconds"].is_number_integer() || row["durationSeconds"].get<int>()<60 || row["durationSeconds"].get<int>()>14400 || (row.contains("subjectId") && !row["subjectId"].is_string()) || (!row.value("subjectId",std::string()).empty() && !subjectIds.count(row.value("subjectId",std::string())))) { sendJson(res,errorJson("Nieprawidłowa sesja skupienia w kopii."),400); return; }
        for (const auto& row : backup["studyCards"]) if (!stringField(row,"id",120) || !stringField(row,"front",500) || !stringField(row,"back",4000) || !stringField(row,"dueAt",40) || !stringField(row,"lastReviewed",40,true) || !stringField(row,"createdAt",40,true) || !row.contains("intervalDays") || !row["intervalDays"].is_number_integer() || row["intervalDays"].get<int>()<0 || row["intervalDays"].get<int>()>365 || !row.contains("repetitions") || !row["repetitions"].is_number_integer() || row["repetitions"].get<int>()<0 || (row.contains("subjectId") && !row["subjectId"].is_string()) || (!row.value("subjectId",std::string()).empty() && !subjectIds.count(row.value("subjectId",std::string())))) { sendJson(res,errorJson("Nieprawidłowa fiszka w kopii."),400); return; }
        std::set<std::string> planIds;
        for (const auto& row : backup["studyPlans"]) {
            if (!stringField(row,"id",120) || !stringField(row,"title",160) || !stringField(row,"targetDate",10,true) || !stringField(row,"notes",1000,true) || (row.contains("subjectId") && !row["subjectId"].is_string()) || (!row.value("subjectId",std::string()).empty() && !subjectIds.count(row.value("subjectId",std::string())))) { sendJson(res,errorJson("Nieprawidłowy plan nauki w kopii."),400); return; }
            if (!planIds.insert(row["id"].get<std::string>()).second) { sendJson(res,errorJson("Duplikat planu nauki w kopii."),400); return; }
        }
        for (const auto& row : backup["studyStages"]) if (!stringField(row,"id",120) || !stringField(row,"planId",120) || !planIds.count(row.value("planId",std::string())) || !stringField(row,"title",200) || !stringField(row,"dueAt",40,true) || !stringField(row,"notes",1000,true) || (row.contains("completed") && !row["completed"].is_boolean()) || (row.contains("stageOrder") && !row["stageOrder"].is_number_integer())) { sendJson(res,errorJson("Nieprawidłowy etap nauki w kopii."),400); return; }

        std::lock_guard<std::mutex> lock(dbMutex);
        if (!execSql("BEGIN IMMEDIATE;")) { sendJson(res,errorJson("Nie można rozpocząć transakcji przywracania."),500); return; }
        bool ok = execSql("DELETE FROM profile_domains; DELETE FROM profile_youtube_channels; DELETE FROM profiles; DELETE FROM subject_sessions; DELETE FROM focus_sessions; DELETE FROM study_cards; DELETE FROM study_stages; DELETE FROM study_plans; DELETE FROM subjects; DELETE FROM calendar_events; DELETE FROM overdue_tasks;");
        auto run = [&](const char* sql, const json& row, const std::vector<std::pair<const char*,int>>& fields) {
            if (!ok) return;
            sqlite3_stmt* stmt=nullptr;
            if (sqlite3_prepare_v2(db,sql,-1,&stmt,nullptr)!=SQLITE_OK) { ok=false; return; }
            int index=1;
            for (const auto& field:fields) {
                const char* key=field.first; int type=field.second;
                if (type==1) sqlite3_bind_int(stmt,index++,row.value(key,false)?1:0);
                else if (type==2) sqlite3_bind_int(stmt,index++,row.value(key,0));
                else { std::string fallback = std::string(key)=="priority" ? "normal" : (std::string(key)=="category" ? "Ogólne" : std::string()); std::string value=row.value(key,fallback); sqlite3_bind_text(stmt,index++,value.c_str(),static_cast<int>(value.size()),SQLITE_TRANSIENT); }
            }
            if (sqlite3_step(stmt)!=SQLITE_DONE) ok=false;
            sqlite3_finalize(stmt);
        };
        for (const auto& row:backup["profiles"]) run("INSERT INTO profiles(id,name,locked) VALUES(?,?,?)",row,{{"id",0},{"name",0},{"locked",1}});
        for (const auto& row:backup["domains"]) run("INSERT INTO profile_domains(id,profile_id,domain) VALUES(?,?,?)",row,{{"id",0},{"profileId",0},{"domain",0}});
        for (const auto& row:backup["youtubeChannels"]) { json normalized=row; std::string handle=normalized.value("handle",std::string()); if (!handle.empty() && handle.front()=='@') handle.erase(handle.begin()); std::transform(handle.begin(),handle.end(),handle.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));}); normalized["handle"]=handle; run("INSERT INTO profile_youtube_channels(id,profile_id,handle) VALUES(?,?,?)",normalized,{{"id",0},{"profileId",0},{"handle",0}}); }
        for (const auto& row:backup["subjects"]) run("INSERT INTO subjects(id,name,color,is_lecture,notes) VALUES(?,?,?,?,?)",row,{{"id",0},{"name",0},{"color",0},{"isLecture",1},{"notes",0}});
        for (const auto& row:backup["events"]) run("INSERT INTO calendar_events(id,title,start_at,end_at) VALUES(?,?,?,?)",row,{{"id",0},{"title",0},{"start",0},{"end",0}});
        for (const auto& row:backup["tasks"]) run("INSERT INTO overdue_tasks(id,title,due_at,notes,completed,priority,category) VALUES(?,?,?,?,?,?,?)",row,{{"id",0},{"title",0},{"dueAt",0},{"notes",0},{"completed",1},{"priority",0},{"category",0}});
        for (const auto& row:backup["sessions"]) run("INSERT INTO subject_sessions(id,subject_id,weekday,start_time,end_time,room,session_type) VALUES(?,?,?,?,?,?,?)",row,{{"id",0},{"subjectId",0},{"weekday",2},{"startTime",0},{"endTime",0},{"room",0},{"sessionType",0}});
        for (const auto& row:backup["focusSessions"]) run("INSERT INTO focus_sessions(id,subject_id,duration_seconds,started_at,ended_at,note) VALUES(?,NULLIF(?,''),?,?,?,?)",row,{{"id",0},{"subjectId",0},{"durationSeconds",2},{"startedAt",0},{"endedAt",0},{"note",0}});
        for (const auto& row:backup["studyCards"]) run("INSERT INTO study_cards(id,subject_id,front,back,due_at,interval_days,repetitions,last_reviewed,created_at) VALUES(?,NULLIF(?,''),?,?,?,?,?,?,?)",row,{{"id",0},{"subjectId",0},{"front",0},{"back",0},{"dueAt",0},{"intervalDays",2},{"repetitions",2},{"lastReviewed",0},{"createdAt",0}});
        for (const auto& row:backup["studyPlans"]) run("INSERT INTO study_plans(id,subject_id,title,target_date,notes) VALUES(?,NULLIF(?,''),?,?,?)",row,{{"id",0},{"subjectId",0},{"title",0},{"targetDate",0},{"notes",0}});
        for (const auto& row:backup["studyStages"]) run("INSERT INTO study_stages(id,plan_id,title,stage_order,due_at,completed,notes,completed_at) VALUES(?,?,?,?,?,?,?,?)",row,{{"id",0},{"planId",0},{"title",0},{"stageOrder",2},{"dueAt",0},{"completed",1},{"notes",0},{"completedAt",0}});
        if (!ok || !execSql("COMMIT;")) {
            execSql("ROLLBACK;"); sendJson(res,errorJson("Przywracanie nie powiodło się. Baza została pozostawiona bez częściowych zmian."),400); return;
        }
        sendJson(res,{{"ok",true},{"restored",{{"profiles",backup["profiles"].size()},{"domains",backup["domains"].size()},{"youtubeChannels",backup["youtubeChannels"].size()},{"subjects",backup["subjects"].size()},{"events",backup["events"].size()},{"tasks",backup["tasks"].size()},{"sessions",backup["sessions"].size()},{"focusSessions",backup["focusSessions"].size()},{"studyCards",backup["studyCards"].size()},{"studyPlans",backup["studyPlans"].size()},{"studyStages",backup["studyStages"].size()}}}});
    });

    server.Get("/api/focus-sessions", [&](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dbMutex); cors(res);
        json rows = json::array(); sqlite3_stmt* stmt = nullptr;
        const char* sql = "SELECT f.id,COALESCE(f.subject_id,''),COALESCE(s.name,''),f.duration_seconds,f.started_at,f.ended_at,f.note FROM focus_sessions f LEFT JOIN subjects s ON s.id=f.subject_id ORDER BY f.started_at DESC LIMIT 200";
        if (sqlite3_prepare_v2(db,sql,-1,&stmt,nullptr)!=SQLITE_OK) { sendJson(res,errorJson("Nie można odczytać historii skupienia."),500); return; }
        while (sqlite3_step(stmt)==SQLITE_ROW) { auto col=[&](int i){const unsigned char* p=sqlite3_column_text(stmt,i);return p?std::string(reinterpret_cast<const char*>(p)):std::string();}; rows.push_back({{"id",col(0)},{"subjectId",col(1)},{"subjectName",col(2)},{"durationSeconds",sqlite3_column_int(stmt,3)},{"startedAt",col(4)},{"endedAt",col(5)},{"note",col(6)}}); }
        sqlite3_finalize(stmt); sendJson(res,{{"focusSessions",rows}});
    });
    server.Post("/api/focus-sessions", [&](const httplib::Request& req, httplib::Response& res) {
        cors(res); json body; try { body=json::parse(req.body); } catch (...) { sendJson(res,errorJson("Niepoprawny JSON."),400); return; }
        if (!body.is_object() || !body.contains("durationSeconds") || !body["durationSeconds"].is_number_integer() || body["durationSeconds"].get<int>()<60 || body["durationSeconds"].get<int>()>14400 || !body.contains("startedAt") || !body["startedAt"].is_string() || body["startedAt"].get<std::string>().size()>40 || !body.contains("endedAt") || !body["endedAt"].is_string() || body["endedAt"].get<std::string>().size()>40 || (body.contains("note") && (!body["note"].is_string() || body["note"].get<std::string>().size()>500)) || (body.contains("subjectId") && !body["subjectId"].is_string())) { sendJson(res,errorJson("Nieprawidłowe dane sesji skupienia."),400); return; }
        const std::string subjectId=body.value("subjectId",std::string()); const std::string startedAt=body["startedAt"].get<std::string>(); const std::string endedAt=body["endedAt"].get<std::string>();
        if (startedAt.empty() || endedAt.empty() || endedAt<startedAt) { sendJson(res,errorJson("Czas zakończenia musi być późniejszy niż rozpoczęcia."),400); return; }
        const std::string id="focus-"+std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
        std::lock_guard<std::mutex> lock(dbMutex);
        if (!subjectId.empty()) { sqlite3_stmt* check=nullptr; sqlite3_prepare_v2(db,"SELECT 1 FROM subjects WHERE id=?",-1,&check,nullptr); sqlite3_bind_text(check,1,subjectId.c_str(),-1,SQLITE_TRANSIENT); bool exists=sqlite3_step(check)==SQLITE_ROW; sqlite3_finalize(check); if(!exists){sendJson(res,errorJson("Wybrany przedmiot nie istnieje."),400);return;} }
        sqlite3_stmt* stmt=nullptr; const char* sql="INSERT INTO focus_sessions(id,subject_id,duration_seconds,started_at,ended_at,note) VALUES(?,?,?,?,?,?)";
        if(sqlite3_prepare_v2(db,sql,-1,&stmt,nullptr)!=SQLITE_OK){sendJson(res,errorJson("Nie można zapisać sesji."),500);return;}
        sqlite3_bind_text(stmt,1,id.c_str(),-1,SQLITE_TRANSIENT); if(subjectId.empty()) sqlite3_bind_null(stmt,2); else sqlite3_bind_text(stmt,2,subjectId.c_str(),-1,SQLITE_TRANSIENT); sqlite3_bind_int(stmt,3,body["durationSeconds"].get<int>()); sqlite3_bind_text(stmt,4,startedAt.c_str(),-1,SQLITE_TRANSIENT); sqlite3_bind_text(stmt,5,endedAt.c_str(),-1,SQLITE_TRANSIENT); std::string note=body.value("note",std::string()); sqlite3_bind_text(stmt,6,note.c_str(),-1,SQLITE_TRANSIENT);
        bool ok=sqlite3_step(stmt)==SQLITE_DONE; sqlite3_finalize(stmt); if(!ok){sendJson(res,errorJson("Nie udało się zapisać sesji skupienia."),500);return;} sendJson(res,{{"ok",true},{"id",id}},201);
    });

    server.Get("/api/review-cards", [&](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dbMutex); cors(res); json rows=json::array(); sqlite3_stmt* stmt=nullptr;
        const char* sql="SELECT c.id,COALESCE(c.subject_id,''),COALESCE(s.name,''),c.front,c.back,c.due_at,c.interval_days,c.repetitions,c.last_reviewed,c.created_at FROM study_cards c LEFT JOIN subjects s ON s.id=c.subject_id ORDER BY c.due_at,c.created_at DESC LIMIT 1000";
        if(sqlite3_prepare_v2(db,sql,-1,&stmt,nullptr)!=SQLITE_OK){sendJson(res,errorJson("Nie można odczytać fiszek."),500);return;}
        while(sqlite3_step(stmt)==SQLITE_ROW){auto col=[&](int i){const unsigned char*p=sqlite3_column_text(stmt,i);return p?std::string(reinterpret_cast<const char*>(p)):std::string();};rows.push_back({{"id",col(0)},{"subjectId",col(1)},{"subjectName",col(2)},{"front",col(3)},{"back",col(4)},{"dueAt",col(5)},{"intervalDays",sqlite3_column_int(stmt,6)},{"repetitions",sqlite3_column_int(stmt,7)},{"lastReviewed",col(8)},{"createdAt",col(9)}});}
        sqlite3_finalize(stmt);sendJson(res,{{"cards",rows}});
    });
    server.Post("/api/review-cards", [&](const httplib::Request& req, httplib::Response& res) {
        cors(res);json body;try{body=json::parse(req.body);}catch(...){sendJson(res,errorJson("Niepoprawny JSON."),400);return;}
        if(!body.is_object()||!body.contains("front")||!body["front"].is_string()||body["front"].get<std::string>().empty()||body["front"].get<std::string>().size()>500||!body.contains("back")||!body["back"].is_string()||body["back"].get<std::string>().empty()||body["back"].get<std::string>().size()>4000||(body.contains("subjectId")&&!body["subjectId"].is_string())){sendJson(res,errorJson("Podaj pytanie i odpowiedź (maks. 500 i 4000 znaków)."),400);return;}
        std::string subjectId=body.value("subjectId",std::string());std::lock_guard<std::mutex> lock(dbMutex);
        if(!subjectId.empty()){sqlite3_stmt*c=nullptr;sqlite3_prepare_v2(db,"SELECT 1 FROM subjects WHERE id=?",-1,&c,nullptr);sqlite3_bind_text(c,1,subjectId.c_str(),-1,SQLITE_TRANSIENT);bool exists=sqlite3_step(c)==SQLITE_ROW;sqlite3_finalize(c);if(!exists){sendJson(res,errorJson("Wybrany przedmiot nie istnieje."),400);return;}}
        auto now=std::chrono::system_clock::now();auto millis=std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();std::string id="card-"+std::to_string(millis);std::string nowIso=body.value("createdAt",std::string());if(nowIso.empty()){std::time_t t=std::chrono::system_clock::to_time_t(now);std::tm tm{};
#ifdef _WIN32
        gmtime_s(&tm,&t);
#else
        gmtime_r(&t,&tm);
#endif
        char nowBuf[32];std::strftime(nowBuf,sizeof(nowBuf),"%Y-%m-%dT%H:%M:%SZ",&tm);nowIso=nowBuf;}
        sqlite3_stmt*stmt=nullptr;if(sqlite3_prepare_v2(db,"INSERT INTO study_cards(id,subject_id,front,back,due_at) VALUES(?,NULLIF(?,''),?,?,?)",-1,&stmt,nullptr)!=SQLITE_OK){sendJson(res,errorJson("Nie można przygotować fiszki."),500);return;}
        std::string due=body.value("dueAt",std::string());if(due.empty())due=nowIso;sqlite3_bind_text(stmt,1,id.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(stmt,2,subjectId.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(stmt,3,body["front"].get<std::string>().c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(stmt,4,body["back"].get<std::string>().c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(stmt,5,due.c_str(),-1,SQLITE_TRANSIENT);bool ok=sqlite3_step(stmt)==SQLITE_DONE;sqlite3_finalize(stmt);if(!ok){sendJson(res,errorJson("Nie udało się zapisać fiszki."),500);return;}sendJson(res,{{"ok",true},{"id",id}},201);
    });
    server.Post(R"(/api/review-cards/([A-Za-z0-9_-]+)/review)", [&](const httplib::Request& req, httplib::Response& res) {
        cors(res);json body;try{body=json::parse(req.body);}catch(...){sendJson(res,errorJson("Niepoprawny JSON."),400);return;}std::string grade=body.value("grade",std::string());if(grade!="again"&&grade!="hard"&&grade!="good"&&grade!="easy"){sendJson(res,errorJson("Nieprawidłowa ocena powtórki."),400);return;}
        std::lock_guard<std::mutex> lock(dbMutex);std::string id=req.matches[1].str();sqlite3_stmt*read=nullptr;if(sqlite3_prepare_v2(db,"SELECT repetitions,interval_days FROM study_cards WHERE id=?",-1,&read,nullptr)!=SQLITE_OK){sendJson(res,errorJson("Nie można odczytać fiszki."),500);return;}sqlite3_bind_text(read,1,id.c_str(),-1,SQLITE_TRANSIENT);if(sqlite3_step(read)!=SQLITE_ROW){sqlite3_finalize(read);sendJson(res,errorJson("Nie znaleziono fiszki."),404);return;}int reps=sqlite3_column_int(read,0),interval=sqlite3_column_int(read,1);sqlite3_finalize(read);
        if(grade=="again"){reps=0;interval=0;}else if(grade=="hard"){interval=std::max(1,interval==0?1:interval+1);reps++;}else if(grade=="good"){interval=interval==0?1:(interval==1?3:std::min(365,interval*2));reps++;}else{interval=interval==0?4:(interval==1?6:std::min(365,interval*3));reps++;}
        auto due=std::chrono::system_clock::now()+std::chrono::hours(24*interval);std::time_t tt=std::chrono::system_clock::to_time_t(due);std::tm tm{};
#ifdef _WIN32
        gmtime_s(&tm,&tt);
#else
        gmtime_r(&tt,&tm);
#endif
        char dueBuf[32];std::strftime(dueBuf,sizeof(dueBuf),"%Y-%m-%dT%H:%M:%SZ",&tm);auto now=std::chrono::system_clock::now();std::time_t nt=std::chrono::system_clock::to_time_t(now);std::tm ntm{};
#ifdef _WIN32
        gmtime_s(&ntm,&nt);
#else
        gmtime_r(&nt,&ntm);
#endif
        char nowBuf[32];std::strftime(nowBuf,sizeof(nowBuf),"%Y-%m-%dT%H:%M:%SZ",&ntm);sqlite3_stmt*up=nullptr;if(sqlite3_prepare_v2(db,"UPDATE study_cards SET repetitions=?,interval_days=?,last_reviewed=?,due_at=? WHERE id=?",-1,&up,nullptr)!=SQLITE_OK){sendJson(res,errorJson("Nie można przygotować aktualizacji fiszki."),500);return;}sqlite3_bind_int(up,1,reps);sqlite3_bind_int(up,2,interval);sqlite3_bind_text(up,3,nowBuf,-1,SQLITE_TRANSIENT);sqlite3_bind_text(up,4,dueBuf,-1,SQLITE_TRANSIENT);sqlite3_bind_text(up,5,id.c_str(),-1,SQLITE_TRANSIENT);bool ok=sqlite3_step(up)==SQLITE_DONE;sqlite3_finalize(up);if(!ok){sendJson(res,errorJson("Nie udało się zapisać powtórki."),500);return;}sendJson(res,{{"ok",true},{"intervalDays",interval},{"repetitions",reps},{"dueAt",dueBuf}});
    });
    server.Delete(R"(/api/review-cards/([A-Za-z0-9_-]+))", [&](const httplib::Request& req, httplib::Response& res) {cors(res);std::lock_guard<std::mutex> lock(dbMutex);sqlite3_stmt*stmt=nullptr;sqlite3_prepare_v2(db,"DELETE FROM study_cards WHERE id=?",-1,&stmt,nullptr);std::string id=req.matches[1].str();sqlite3_bind_text(stmt,1,id.c_str(),-1,SQLITE_TRANSIENT);bool ok=sqlite3_step(stmt)==SQLITE_DONE;int changed=sqlite3_changes(db);sqlite3_finalize(stmt);if(!ok||!changed){sendJson(res,errorJson("Nie znaleziono fiszki."),404);return;}sendJson(res,{{"ok",true}});});

    // Study roadmaps: plans are independent from task lists and support ordered stages.
    server.Get("/api/study-plans", [&](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dbMutex); cors(res); json plans=json::array(); sqlite3_stmt* stmt=nullptr;
        const char* sql="SELECT p.id,COALESCE(p.subject_id,''),COALESCE(s.name,''),p.title,p.target_date,p.notes,p.created_at,p.updated_at FROM study_plans p LEFT JOIN subjects s ON s.id=p.subject_id ORDER BY CASE WHEN p.target_date='' THEN 1 ELSE 0 END,p.target_date,p.created_at DESC";
        if(sqlite3_prepare_v2(db,sql,-1,&stmt,nullptr)!=SQLITE_OK){sendJson(res,errorJson("Nie można odczytać planów nauki."),500);return;}
        while(sqlite3_step(stmt)==SQLITE_ROW){auto col=[](sqlite3_stmt*q,int i){const unsigned char*x=sqlite3_column_text(q,i);return x?std::string(reinterpret_cast<const char*>(x)):std::string();};json plan={{"id",col(stmt,0)},{"subjectId",col(stmt,1)},{"subjectName",col(stmt,2)},{"title",col(stmt,3)},{"targetDate",col(stmt,4)},{"notes",col(stmt,5)},{"createdAt",col(stmt,6)},{"updatedAt",col(stmt,7)},{"stages",json::array()}};sqlite3_stmt* stage=nullptr;if(sqlite3_prepare_v2(db,"SELECT id,title,stage_order,due_at,completed,notes,completed_at FROM study_stages WHERE plan_id=? ORDER BY stage_order,created_at",-1,&stage,nullptr)==SQLITE_OK){sqlite3_bind_text(stage,1,col(stmt,0).c_str(),-1,SQLITE_TRANSIENT);while(sqlite3_step(stage)==SQLITE_ROW){auto sc=[](sqlite3_stmt*q,int i){const unsigned char*x=sqlite3_column_text(q,i);return x?std::string(reinterpret_cast<const char*>(x)):std::string();};plan["stages"].push_back({{"id",sc(stage,0)},{"title",sc(stage,1)},{"stageOrder",sqlite3_column_int(stage,2)},{"dueAt",sc(stage,3)},{"completed",sqlite3_column_int(stage,4)!=0},{"notes",sc(stage,5)},{"completedAt",sc(stage,6)}});}sqlite3_finalize(stage);}plans.push_back(plan);}
        sqlite3_finalize(stmt);sendJson(res,{{"plans",plans}});
    });
    server.Post("/api/study-plans", [&](const httplib::Request& req, httplib::Response& res) {
        cors(res);json b;try{b=json::parse(req.body);}catch(...){sendJson(res,errorJson("Niepoprawny JSON."),400);return;}
        if(!b.is_object()||!b.contains("title")||!b["title"].is_string()||b["title"].get<std::string>().empty()||b["title"].get<std::string>().size()>160||!b.contains("targetDate")||!b["targetDate"].is_string()||b["targetDate"].get<std::string>().size()>10||!b.contains("notes")||!b["notes"].is_string()||b["notes"].get<std::string>().size()>1000||(b.contains("subjectId")&&!b["subjectId"].is_string())){sendJson(res,errorJson("Podaj tytuł planu, prawidłowy termin i notatkę (maks. 1000 znaków)."),400);return;}
        std::string sid=b.value("subjectId",std::string());std::lock_guard<std::mutex> lock(dbMutex);if(!sid.empty()){sqlite3_stmt*c=nullptr;sqlite3_prepare_v2(db,"SELECT 1 FROM subjects WHERE id=?",-1,&c,nullptr);sqlite3_bind_text(c,1,sid.c_str(),-1,SQLITE_TRANSIENT);bool exists=sqlite3_step(c)==SQLITE_ROW;sqlite3_finalize(c);if(!exists){sendJson(res,errorJson("Wybrany przedmiot nie istnieje."),400);return;}}
        std::string id="plan-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());sqlite3_stmt*st=nullptr;if(sqlite3_prepare_v2(db,"INSERT INTO study_plans(id,subject_id,title,target_date,notes) VALUES(?,NULLIF(?,''),?,?,?)",-1,&st,nullptr)!=SQLITE_OK){sendJson(res,errorJson("Nie można przygotować planu."),500);return;}std::string title=b["title"],date=b["targetDate"],notes=b["notes"];sqlite3_bind_text(st,1,id.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(st,2,sid.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(st,3,title.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(st,4,date.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(st,5,notes.c_str(),-1,SQLITE_TRANSIENT);bool ok=sqlite3_step(st)==SQLITE_DONE;sqlite3_finalize(st);if(!ok){sendJson(res,errorJson("Nie udało się zapisać planu."),500);return;}sendJson(res,{{"ok",true},{"id",id}},201);
    });
    server.Post(R"(/api/study-plans/([A-Za-z0-9_-]+)/stages)", [&](const httplib::Request& req, httplib::Response& res) {
        cors(res);json b;try{b=json::parse(req.body);}catch(...){sendJson(res,errorJson("Niepoprawny JSON."),400);return;}if(!b.is_object()||!b.contains("title")||!b["title"].is_string()||b["title"].get<std::string>().empty()||b["title"].get<std::string>().size()>200||(b.contains("dueAt")&&!b["dueAt"].is_string())||(b.contains("notes")&&!b["notes"].is_string())||b.value("notes",std::string()).size()>1000){sendJson(res,errorJson("Podaj nazwę etapu (maks. 200 znaków) i poprawne notatki."),400);return;}
        std::lock_guard<std::mutex> lock(dbMutex);std::string plan=req.matches[1].str();sqlite3_stmt*exists=nullptr;sqlite3_prepare_v2(db,"SELECT COALESCE(MAX(stage_order),0)+1 FROM study_stages WHERE plan_id=?",-1,&exists,nullptr);sqlite3_bind_text(exists,1,plan.c_str(),-1,SQLITE_TRANSIENT);int order=sqlite3_step(exists)==SQLITE_ROW?sqlite3_column_int(exists,0):1;sqlite3_finalize(exists);sqlite3_stmt*check=nullptr;sqlite3_prepare_v2(db,"SELECT 1 FROM study_plans WHERE id=?",-1,&check,nullptr);sqlite3_bind_text(check,1,plan.c_str(),-1,SQLITE_TRANSIENT);bool found=sqlite3_step(check)==SQLITE_ROW;sqlite3_finalize(check);if(!found){sendJson(res,errorJson("Nie znaleziono planu."),404);return;}std::string id="stage-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());std::string title=b["title"],due=b.value("dueAt",std::string()),notes=b.value("notes",std::string());sqlite3_stmt*st=nullptr;sqlite3_prepare_v2(db,"INSERT INTO study_stages(id,plan_id,title,stage_order,due_at,notes) VALUES(?,?,?,?,?,?)",-1,&st,nullptr);sqlite3_bind_text(st,1,id.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(st,2,plan.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(st,3,title.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_int(st,4,order);sqlite3_bind_text(st,5,due.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(st,6,notes.c_str(),-1,SQLITE_TRANSIENT);bool ok=sqlite3_step(st)==SQLITE_DONE;sqlite3_finalize(st);if(!ok){sendJson(res,errorJson("Nie udało się dodać etapu."),500);return;}sendJson(res,{{"ok",true},{"id",id}},201);
    });
    server.Put(R"(/api/study-stages/([A-Za-z0-9_-]+)/complete)", [&](const httplib::Request& req, httplib::Response& res) {cors(res);json b;try{b=json::parse(req.body);}catch(...){sendJson(res,errorJson("Niepoprawny JSON."),400);return;}if(!b.is_object()||!b.contains("completed")||!b["completed"].is_boolean()){sendJson(res,errorJson("Pole completed musi być logiczne."),400);return;}std::lock_guard<std::mutex> lock(dbMutex);std::string id=req.matches[1].str();sqlite3_stmt*st=nullptr;sqlite3_prepare_v2(db,"UPDATE study_stages SET completed=?,completed_at=CASE WHEN ?=1 THEN CURRENT_TIMESTAMP ELSE '' END WHERE id=?",-1,&st,nullptr);int val=b["completed"]?1:0;sqlite3_bind_int(st,1,val);sqlite3_bind_int(st,2,val);sqlite3_bind_text(st,3,id.c_str(),-1,SQLITE_TRANSIENT);bool ok=sqlite3_step(st)==SQLITE_DONE;int changed=sqlite3_changes(db);sqlite3_finalize(st);if(!ok||!changed){sendJson(res,errorJson("Nie znaleziono etapu."),404);return;}sendJson(res,{{"ok",true}});});
    server.Delete(R"(/api/study-stages/([A-Za-z0-9_-]+))", [&](const httplib::Request& req, httplib::Response& res) {cors(res);std::lock_guard<std::mutex> lock(dbMutex);sqlite3_stmt*st=nullptr;sqlite3_prepare_v2(db,"DELETE FROM study_stages WHERE id=?",-1,&st,nullptr);std::string id=req.matches[1].str();sqlite3_bind_text(st,1,id.c_str(),-1,SQLITE_TRANSIENT);bool ok=sqlite3_step(st)==SQLITE_DONE;int changed=sqlite3_changes(db);sqlite3_finalize(st);if(!ok||!changed){sendJson(res,errorJson("Nie znaleziono etapu."),404);return;}sendJson(res,{{"ok",true}});});
    server.Delete(R"(/api/study-plans/([A-Za-z0-9_-]+))", [&](const httplib::Request& req, httplib::Response& res) {cors(res);std::lock_guard<std::mutex> lock(dbMutex);sqlite3_stmt*st=nullptr;sqlite3_prepare_v2(db,"DELETE FROM study_plans WHERE id=?",-1,&st,nullptr);std::string id=req.matches[1].str();sqlite3_bind_text(st,1,id.c_str(),-1,SQLITE_TRANSIENT);bool ok=sqlite3_step(st)==SQLITE_DONE;int changed=sqlite3_changes(db);sqlite3_finalize(st);if(!ok||!changed){sendJson(res,errorJson("Nie znaleziono planu."),404);return;}sendJson(res,{{"ok",true}});});

    server.Get("/api/windows/status", [&](const httplib::Request&, httplib::Response& res) { cors(res); sendJson(res, windowsStatus()); });
    server.Get("/api/windows/rules", [&](const httplib::Request&, httplib::Response& res) {
      cors(res); json rules=json::array(); std::lock_guard<std::mutex> lock(dbMutex); sqlite3_stmt*st=nullptr;
      if(sqlite3_prepare_v2(db,"SELECT id,rule_type,pattern,enabled,created_at FROM windows_block_rules ORDER BY created_at DESC",-1,&st,nullptr)!=SQLITE_OK){sendJson(res,errorJson("Nie można odczytać reguł Windows."),500);return;}
      while(sqlite3_step(st)==SQLITE_ROW){auto col=[&](int i){const unsigned char*p=sqlite3_column_text(st,i);return p?std::string(reinterpret_cast<const char*>(p)):std::string();};rules.push_back({{"id",col(0)},{"type",col(1)},{"pattern",col(2)},{"enabled",sqlite3_column_int(st,3)!=0},{"createdAt",col(4)}});}sqlite3_finalize(st);sendJson(res,{{"rules",rules}});
    });
    server.Post("/api/windows/rules", [&](const httplib::Request& req, httplib::Response& res) {
      cors(res);json b;try{b=json::parse(req.body);}catch(...){sendJson(res,errorJson("Niepoprawny JSON."),400);return;}
      if(!b.is_object()||!b.contains("type")||!b["type"].is_string()||!b.contains("pattern")||!b["pattern"].is_string()){sendJson(res,errorJson("Wymagane są pola type i pattern."),400);return;}
      std::string type=b["type"],pattern=b["pattern"];if(type!="process"&&type!="title"&&type!="path"){sendJson(res,errorJson("Typ musi być process, title lub path."),400);return;}if(pattern.empty()||pattern.size()>260||pattern.find_first_of("\r\n")!=std::string::npos){sendJson(res,errorJson("Wzorzec musi mieć 1–260 znaków i jedną linię."),400);return;}
      std::string lower=lowerAscii(pattern);if(type=="process"){if(lower.find('/')!=std::string::npos||lower.find('\\')!=std::string::npos||lower=="amogus_core.exe"||lower=="explorer.exe"||lower=="system.exe"){sendJson(res,errorJson("Podaj nazwę aplikacji .exe; tej nazwy nie można blokować."),400);return;}if(lower.size()<5||lower.substr(lower.size()-4)!=".exe"){sendJson(res,errorJson("Reguła procesu musi wskazywać plik .exe, np. discord.exe."),400);return;}}
      std::string id="wr_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());std::lock_guard<std::mutex> lock(dbMutex);sqlite3_stmt*st=nullptr;const char*sql="INSERT INTO windows_block_rules(id,rule_type,pattern,enabled) VALUES(?,?,?,1) ON CONFLICT(rule_type,pattern) DO UPDATE SET enabled=1";if(sqlite3_prepare_v2(db,sql,-1,&st,nullptr)!=SQLITE_OK){sendJson(res,errorJson("Database prepare failed"),500);return;}sqlite3_bind_text(st,1,id.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(st,2,type.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(st,3,pattern.c_str(),-1,SQLITE_TRANSIENT);int rc=sqlite3_step(st);sqlite3_finalize(st);if(rc!=SQLITE_DONE){sendJson(res,errorJson("Nie udało się zapisać reguły."),500);return;}sendJson(res,{{"ok",true}});
    });
    server.Delete(R"(/api/windows/rules/([A-Za-z0-9_-]+))", [&](const httplib::Request& req, httplib::Response& res) {cors(res);std::lock_guard<std::mutex> lock(dbMutex);sqlite3_stmt*st=nullptr;sqlite3_prepare_v2(db,"DELETE FROM windows_block_rules WHERE id=?",-1,&st,nullptr);std::string id=req.matches[1].str();sqlite3_bind_text(st,1,id.c_str(),-1,SQLITE_TRANSIENT);int rc=sqlite3_step(st),changed=sqlite3_changes(db);sqlite3_finalize(st);if(rc!=SQLITE_DONE||!changed){sendJson(res,errorJson("Nie znaleziono reguły."),404);return;}sendJson(res,{{"ok",true}});});
    server.Post("/api/windows/blocker", [&](const httplib::Request& req, httplib::Response& res) {cors(res);json b;try{b=json::parse(req.body);}catch(...){sendJson(res,errorJson("Niepoprawny JSON."),400);return;}if(!b.is_object()||!b.contains("enabled")||!b["enabled"].is_boolean()){sendJson(res,errorJson("Pole enabled musi być logiczne."),400);return;}
#ifdef _WIN32
      if(!saveWindowsBlockerSetting(b["enabled"].get<bool>())){sendJson(res,errorJson("Nie udało się zapisać ustawienia."),500);return;}windowsBlockerEnabled.store(b["enabled"].get<bool>());sendJson(res,{{"ok",true},{"status",windowsStatus()}});
#else
      sendJson(res,errorJson("Blokowanie procesów jest dostępne wyłącznie w Windows."),501);
#endif
    });
    server.Post("/api/windows/autostart", [&](const httplib::Request& req, httplib::Response& res) {cors(res);json b;try{b=json::parse(req.body);}catch(...){sendJson(res,errorJson("Niepoprawny JSON."),400);return;}if(!b.is_object()||!b.contains("enabled")||!b["enabled"].is_boolean()){sendJson(res,errorJson("Pole enabled musi być logiczne."),400);return;}
#ifdef _WIN32
      bool ok=setWindowsAutostart(b["enabled"].get<bool>());if(!ok){sendJson(res,errorJson("Nie udało się zmienić autostartu użytkownika."),500);return;}sendJson(res,{{"ok",true},{"status",windowsStatus()}});
#else
      sendJson(res,errorJson("Autostart jest dostępny wyłącznie w Windows."),501);
#endif
    });

    server.Get("/api/health", [&](const httplib::Request&, httplib::Response& res) {
        cors(res); sendJson(res, {{"ok", true}, {"service", "amogus-core"}, {"version", "0.30.0"}});
    });
    server.Get("/api/profiles", [&](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dbMutex); cors(res); sendJson(res, {{"profiles", listProfiles()}});
    });
    // Read-only snapshot for the optional Chrome/Edge extension. Only GET is
    // permitted from chrome-extension origins by the pre-routing guard.
    server.Get("/api/blocking/config", [&](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dbMutex);
        json profiles = listProfiles();
        json domains = json::array();
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, "SELECT profile_id,domain FROM profile_domains ORDER BY profile_id,domain COLLATE NOCASE", -1, &stmt, nullptr) != SQLITE_OK) {
            sendJson(res, errorJson("Nie można odczytać reguł blokowania."), 500); return;
        }
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const auto* p = sqlite3_column_text(stmt, 0);
            const auto* d = sqlite3_column_text(stmt, 1);
            domains.push_back({{"profileId", p ? reinterpret_cast<const char*>(p) : ""}, {"domain", d ? reinterpret_cast<const char*>(d) : ""}});
        }
        sqlite3_finalize(stmt);
        json youtubeChannels = json::array();
        if (sqlite3_prepare_v2(db, "SELECT profile_id,handle FROM profile_youtube_channels ORDER BY profile_id,handle COLLATE NOCASE", -1, &stmt, nullptr) != SQLITE_OK) { sendJson(res,errorJson("Nie można odczytać kanałów YouTube."),500); return; }
        while (sqlite3_step(stmt) == SQLITE_ROW) { const auto* p=sqlite3_column_text(stmt,0); const auto* h=sqlite3_column_text(stmt,1); youtubeChannels.push_back({{"profileId",p?reinterpret_cast<const char*>(p):""},{"handle",h?reinterpret_cast<const char*>(h):""}}); }
        sqlite3_finalize(stmt);
        sendJson(res, {{"profiles", profiles}, {"domains", domains}, {"youtubeChannels", youtubeChannels}});
    });
    server.Post("/api/profiles", [&](const httplib::Request& req, httplib::Response& res) {
        cors(res);
        json body;
        try { body = json::parse(req.body); } catch (...) { sendJson(res, errorJson("Invalid JSON body"), 400); return; }
        if (!body.is_object() || !body.contains("name") || !body["name"].is_string()) {
            sendJson(res, errorJson("Field 'name' must be a string"), 400); return;
        }
        std::string name = body["name"].get<std::string>();
        if (name.empty() || name.size() > 100) { sendJson(res, errorJson("Name must contain 1-100 bytes"), 400); return; }
        // Bound input and use a prepared statement to avoid SQL injection.
        std::string id = "profile-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        std::lock_guard<std::mutex> lock(dbMutex);
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, "INSERT INTO profiles(id,name,locked) VALUES(?,?,0)", -1, &stmt, nullptr) != SQLITE_OK) {
            sendJson(res, errorJson("Database prepare failed"), 500); return;
        }
        sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, name.c_str(), static_cast<int>(name.size()), SQLITE_TRANSIENT);
        int rc = sqlite3_step(stmt); sqlite3_finalize(stmt);
        if (rc != SQLITE_DONE) { sendJson(res, errorJson("Database write failed"), 500); return; }
        sendJson(res, {{"ok", true}, {"profile", {{"id", id}, {"name", name}, {"locked", false}}}}, 201);
    });

    server.Put(R"(/api/profiles/([A-Za-z0-9_-]+))", [&](const httplib::Request& req, httplib::Response& res) {
        cors(res); json body;
        try { body = json::parse(req.body); } catch (...) { sendJson(res,errorJson("Niepoprawny JSON."),400); return; }
        if (!body.is_object() || !body.contains("name") || !body["name"].is_string()) { sendJson(res,errorJson("Podaj nazwę profilu."),400); return; }
        std::string name=body["name"].get<std::string>();
        while(!name.empty() && std::isspace(static_cast<unsigned char>(name.front()))) name.erase(name.begin());
        while(!name.empty() && std::isspace(static_cast<unsigned char>(name.back()))) name.pop_back();
        if(name.empty() || name.size()>100) { sendJson(res,errorJson("Nazwa profilu musi mieć 1–100 znaków."),400); return; }
        const std::string id=req.matches[1].str(); std::lock_guard<std::mutex> lock(dbMutex); sqlite3_stmt* check=nullptr;
        if(sqlite3_prepare_v2(db,"SELECT locked FROM profiles WHERE id=?",-1,&check,nullptr)!=SQLITE_OK){sendJson(res,errorJson("Błąd bazy danych."),500);return;}
        sqlite3_bind_text(check,1,id.c_str(),-1,SQLITE_TRANSIENT); int rc=sqlite3_step(check);
        if(rc!=SQLITE_ROW){sqlite3_finalize(check);sendJson(res,errorJson("Nie znaleziono profilu."),404);return;}
        bool locked=sqlite3_column_int(check,0)!=0; sqlite3_finalize(check);
        if(locked){sendJson(res,errorJson("Profil jest zablokowany. Odblokuj go przed zmianą nazwy."),409);return;}
        sqlite3_stmt* stmt=nullptr; if(sqlite3_prepare_v2(db,"UPDATE profiles SET name=?,updated_at=CURRENT_TIMESTAMP WHERE id=?",-1,&stmt,nullptr)!=SQLITE_OK){sendJson(res,errorJson("Nie można przygotować zmiany nazwy."),500);return;}
        sqlite3_bind_text(stmt,1,name.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(stmt,2,id.c_str(),-1,SQLITE_TRANSIENT);rc=sqlite3_step(stmt);sqlite3_finalize(stmt);
        if(rc!=SQLITE_DONE){sendJson(res,errorJson("Nie udało się zmienić nazwy profilu."),500);return;}
        sendJson(res,{{"ok",true},{"profile",{{"id",id},{"name",name},{"locked",false}}}});
    });

    // Profile lock protects its allow-list configuration from accidental edits.
    // Unlocking requires an explicit Japanese commitment phrase; this is a
    // deliberate-friction feature, not an authentication boundary.
    server.Put(R"(/api/profiles/([A-Za-z0-9_-]+)/lock)", [&](const httplib::Request& req, httplib::Response& res) {
        cors(res); json body;
        try { body = json::parse(req.body); } catch (...) { sendJson(res, errorJson("Niepoprawny JSON."), 400); return; }
        if (!body.is_object() || !body.contains("locked") || !body["locked"].is_boolean()) { sendJson(res, errorJson("Pole locked musi być logiczne."), 400); return; }
        const std::string id = req.matches[1].str(); const bool locked = body["locked"].get<bool>();
        if (!locked && (!body.contains("ritual") || !body["ritual"].is_string() || body["ritual"].get<std::string>() != "私は自分の目標を守ります")) {
            sendJson(res, errorJson("Rytuał odblokowania nie został wykonany poprawnie."), 403); return;
        }
        std::lock_guard<std::mutex> lock(dbMutex); sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, "UPDATE profiles SET locked=?,updated_at=CURRENT_TIMESTAMP WHERE id=?", -1, &stmt, nullptr) != SQLITE_OK) { sendJson(res, errorJson("Nie można przygotować zmiany blokady."), 500); return; }
        sqlite3_bind_int(stmt, 1, locked ? 1 : 0); sqlite3_bind_text(stmt, 2, id.c_str(), -1, SQLITE_TRANSIENT);
        const int rc = sqlite3_step(stmt); const int changed = sqlite3_changes(db); sqlite3_finalize(stmt);
        if (rc != SQLITE_DONE || !changed) { sendJson(res, errorJson("Nie znaleziono profilu."), 404); return; }
        sendJson(res, {{"ok", true}, {"locked", locked}});
    });

    server.Delete(R"(/api/profiles/([A-Za-z0-9_-]+))", [&](const httplib::Request& req, httplib::Response& res) {
        cors(res); const std::string id = req.matches[1].str();
        if (id == "default") { sendJson(res, errorJson("Profil domyślny nie może zostać usunięty."), 400); return; }
        std::lock_guard<std::mutex> lock(dbMutex); sqlite3_stmt* guard = nullptr;
        if (sqlite3_prepare_v2(db, "SELECT locked FROM profiles WHERE id=?", -1, &guard, nullptr) == SQLITE_OK) { sqlite3_bind_text(guard,1,id.c_str(),-1,SQLITE_TRANSIENT); if (sqlite3_step(guard)==SQLITE_ROW && sqlite3_column_int(guard,0)!=0) { sqlite3_finalize(guard); sendJson(res,errorJson("Najpierw odblokuj profil."),409); return; } sqlite3_finalize(guard); }
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, "DELETE FROM profiles WHERE id=?", -1, &stmt, nullptr) != SQLITE_OK) { sendJson(res, errorJson("Database prepare failed"), 500); return; }
        sqlite3_bind_text(stmt,1,id.c_str(),-1,SQLITE_TRANSIENT); int rc=sqlite3_step(stmt), changed=sqlite3_changes(db); sqlite3_finalize(stmt);
        if (rc != SQLITE_DONE) { sendJson(res,errorJson("Database delete failed"),500); return; }
        if (!changed) { sendJson(res,errorJson("Profile not found"),404); return; }
        sendJson(res,{{"ok",true}});
    });

    // Domain entries are profile allow-list exceptions consumed by the browser extension.
    server.Get(R"(/api/profiles/([A-Za-z0-9_-]+)/domains)", [&](const httplib::Request& req, httplib::Response& res) {
        cors(res);
        std::lock_guard<std::mutex> lock(dbMutex);
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, "SELECT id,domain,created_at FROM profile_domains WHERE profile_id=? ORDER BY domain COLLATE NOCASE", -1, &stmt, nullptr) != SQLITE_OK) {
            sendJson(res, errorJson("Database read failed"), 500); return;
        }
        std::string profileId = req.matches[1].str();
        sqlite3_bind_text(stmt, 1, profileId.c_str(), -1, SQLITE_TRANSIENT);
        json domains = json::array();
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            auto col = [&](int i) { const unsigned char* p = sqlite3_column_text(stmt, i); return p ? std::string(reinterpret_cast<const char*>(p)) : std::string(); };
            domains.push_back({{"id",col(0)},{"domain",col(1)},{"createdAt",col(2)}});
        }
        sqlite3_finalize(stmt);
        sendJson(res, {{"domains", domains}});
    });
    server.Post(R"(/api/profiles/([A-Za-z0-9_-]+)/domains)", [&](const httplib::Request& req, httplib::Response& res) {
        cors(res);
        json body;
        try { body = json::parse(req.body); } catch (...) { sendJson(res, errorJson("Invalid JSON body"), 400); return; }
        if (!body.is_object() || !body.contains("domain") || !body["domain"].is_string()) {
            sendJson(res, errorJson("Field 'domain' must be a string"), 400); return;
        }
        std::string domain = body["domain"].get<std::string>();
        while (!domain.empty() && std::isspace(static_cast<unsigned char>(domain.front()))) domain.erase(domain.begin());
        while (!domain.empty() && std::isspace(static_cast<unsigned char>(domain.back()))) domain.pop_back();
        std::transform(domain.begin(), domain.end(), domain.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (domain.rfind("https://", 0) == 0) domain.erase(0, 8);
        else if (domain.rfind("http://", 0) == 0) domain.erase(0, 7);
        if (!domain.empty() && domain.rfind("www.", 0) == 0) domain.erase(0, 4);
        const auto slash = domain.find_first_of("/?#");
        if (slash != std::string::npos) domain.resize(slash);
        bool valid = !domain.empty() && domain.size() <= 253 && domain.find('.') != std::string::npos && domain.front() != '.' && domain.back() != '.';
        for (unsigned char c : domain) {
            if (!(std::isalnum(c) || c == '.' || c == '-')) { valid = false; break; }
        }
        if (!valid) { sendJson(res, errorJson("Podaj domenę, np. youtube.com, bez ścieżki ani portu."), 400); return; }
        const std::string profileId = req.matches[1].str();
        std::lock_guard<std::mutex> lock(dbMutex);
        sqlite3_stmt* check = nullptr;
        if (sqlite3_prepare_v2(db, "SELECT 1 FROM profiles WHERE id=?", -1, &check, nullptr) != SQLITE_OK) { sendJson(res, errorJson("Database prepare failed"), 500); return; }
        sqlite3_bind_text(check, 1, profileId.c_str(), -1, SQLITE_TRANSIENT);
        const bool profileExists = sqlite3_step(check) == SQLITE_ROW;
        sqlite3_finalize(check);
        if (!profileExists) { sendJson(res, errorJson("Profile not found"), 404); return; }
        sqlite3_stmt* lockCheck = nullptr; sqlite3_prepare_v2(db, "SELECT locked FROM profiles WHERE id=?", -1, &lockCheck, nullptr); sqlite3_bind_text(lockCheck,1,profileId.c_str(),-1,SQLITE_TRANSIENT); const bool profileLocked = sqlite3_step(lockCheck)==SQLITE_ROW && sqlite3_column_int(lockCheck,0)!=0; sqlite3_finalize(lockCheck);
        if (profileLocked) { sendJson(res,errorJson("Profil jest zablokowany. Wykonaj rytuał odblokowania, aby zmienić domeny."),409); return; }
        const std::string id = "domain-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, "INSERT OR IGNORE INTO profile_domains(id,profile_id,domain) VALUES(?,?,?)", -1, &stmt, nullptr) != SQLITE_OK) { sendJson(res, errorJson("Database prepare failed"), 500); return; }
        sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT); sqlite3_bind_text(stmt, 2, profileId.c_str(), -1, SQLITE_TRANSIENT); sqlite3_bind_text(stmt, 3, domain.c_str(), -1, SQLITE_TRANSIENT);
        const int rc = sqlite3_step(stmt); const int changed = sqlite3_changes(db); sqlite3_finalize(stmt);
        if (rc != SQLITE_DONE) { sendJson(res, errorJson("Database write failed"), 500); return; }
        if (!changed) { sendJson(res, errorJson("Ta domena jest już przypisana do tego profilu."), 409); return; }
        sendJson(res, {{"ok",true},{"domain",{{"id",id},{"domain",domain}}}}, 201);
    });
    server.Delete(R"(/api/profiles/([A-Za-z0-9_-]+)/domains/([A-Za-z0-9_-]+))", [&](const httplib::Request& req, httplib::Response& res) {
        cors(res);
        std::lock_guard<std::mutex> lock(dbMutex);
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, "DELETE FROM profile_domains WHERE profile_id=? AND id=?", -1, &stmt, nullptr) != SQLITE_OK) { sendJson(res, errorJson("Database prepare failed"), 500); return; }
        const std::string profileId = req.matches[1].str(), domainId = req.matches[2].str();
        sqlite3_stmt* lockCheck = nullptr; sqlite3_prepare_v2(db, "SELECT locked FROM profiles WHERE id=?", -1, &lockCheck, nullptr); sqlite3_bind_text(lockCheck,1,profileId.c_str(),-1,SQLITE_TRANSIENT); const bool profileLocked = sqlite3_step(lockCheck)==SQLITE_ROW && sqlite3_column_int(lockCheck,0)!=0; sqlite3_finalize(lockCheck);
        if (profileLocked) { sqlite3_finalize(stmt); sendJson(res,errorJson("Profil jest zablokowany. Wykonaj rytuał odblokowania, aby zmienić domeny."),409); return; }
        sqlite3_bind_text(stmt, 1, profileId.c_str(), -1, SQLITE_TRANSIENT); sqlite3_bind_text(stmt, 2, domainId.c_str(), -1, SQLITE_TRANSIENT);
        const int rc = sqlite3_step(stmt); const int changed = sqlite3_changes(db); sqlite3_finalize(stmt);
        if (rc != SQLITE_DONE) { sendJson(res, errorJson("Database delete failed"), 500); return; }
        if (!changed) { sendJson(res, errorJson("Domain rule not found"), 404); return; }
        sendJson(res, {{"ok",true}});
    });

    // YouTube allow-list: handles are exact, profile-scoped and independent of domain rules.
    server.Get(R"(/api/profiles/([A-Za-z0-9_-]+)/youtube-channels)", [&](const httplib::Request& req, httplib::Response& res) {
        cors(res); std::lock_guard<std::mutex> lock(dbMutex); const std::string profileId=req.matches[1].str();
        sqlite3_stmt* stmt=nullptr; if(sqlite3_prepare_v2(db,"SELECT id,handle,created_at FROM profile_youtube_channels WHERE profile_id=? ORDER BY handle COLLATE NOCASE",-1,&stmt,nullptr)!=SQLITE_OK){sendJson(res,errorJson("Database read failed"),500);return;}
        sqlite3_bind_text(stmt,1,profileId.c_str(),-1,SQLITE_TRANSIENT); json channels=json::array();
        while(sqlite3_step(stmt)==SQLITE_ROW){auto col=[&](int i){const unsigned char*p=sqlite3_column_text(stmt,i);return p?std::string(reinterpret_cast<const char*>(p)):std::string();};channels.push_back({{"id",col(0)},{"handle",col(1)},{"createdAt",col(2)}});} sqlite3_finalize(stmt); sendJson(res,{{"channels",channels}});
    });
    server.Post(R"(/api/profiles/([A-Za-z0-9_-]+)/youtube-channels)", [&](const httplib::Request& req, httplib::Response& res) {
        cors(res); json body; try{body=json::parse(req.body);}catch(...){sendJson(res,errorJson("Invalid JSON body"),400);return;}
        if(!body.is_object()||!body.contains("handle")||!body["handle"].is_string()){sendJson(res,errorJson("Pole handle musi być tekstem, np. @konopskyy."),400);return;}
        std::string handle=body["handle"].get<std::string>(); while(!handle.empty()&&std::isspace(static_cast<unsigned char>(handle.front())))handle.erase(handle.begin()); while(!handle.empty()&&std::isspace(static_cast<unsigned char>(handle.back())))handle.pop_back(); if(!handle.empty()&&handle.front()=='@')handle.erase(handle.begin()); std::transform(handle.begin(),handle.end(),handle.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
        bool valid=handle.size()>=3&&handle.size()<=30&&std::all_of(handle.begin(),handle.end(),[](unsigned char c){return std::isalnum(c)||c=='_'||c=='-'||c=='.';}); if(!valid){sendJson(res,errorJson("Podaj uchwyt @kanału o długości 3–30 znaków (litery, cyfry, kropki, podkreślenia lub myślniki)."),400);return;}
        const std::string profileId=req.matches[1].str(); std::lock_guard<std::mutex> lock(dbMutex); sqlite3_stmt* check=nullptr;
        if(sqlite3_prepare_v2(db,"SELECT locked FROM profiles WHERE id=?",-1,&check,nullptr)!=SQLITE_OK){sendJson(res,errorJson("Database prepare failed"),500);return;} sqlite3_bind_text(check,1,profileId.c_str(),-1,SQLITE_TRANSIENT); int checkRc=sqlite3_step(check); if(checkRc!=SQLITE_ROW){sqlite3_finalize(check);sendJson(res,errorJson("Profile not found"),404);return;} bool locked=sqlite3_column_int(check,0)!=0; sqlite3_finalize(check); if(locked){sendJson(res,errorJson("Profil jest zablokowany. Odblokuj go przed edycją kanałów."),409);return;}
        const std::string id="yt-channel-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()); sqlite3_stmt* stmt=nullptr; if(sqlite3_prepare_v2(db,"INSERT OR IGNORE INTO profile_youtube_channels(id,profile_id,handle) VALUES(?,?,?)",-1,&stmt,nullptr)!=SQLITE_OK){sendJson(res,errorJson("Database prepare failed"),500);return;} sqlite3_bind_text(stmt,1,id.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(stmt,2,profileId.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(stmt,3,handle.c_str(),-1,SQLITE_TRANSIENT);int rc=sqlite3_step(stmt),changed=sqlite3_changes(db);sqlite3_finalize(stmt);if(rc!=SQLITE_DONE){sendJson(res,errorJson("Database write failed"),500);return;}if(!changed){sendJson(res,errorJson("Ten kanał jest już przypisany do profilu."),409);return;}sendJson(res,{{"ok",true},{"channel",{{"id",id},{"handle",handle}}}},201);
    });
    server.Delete(R"(/api/profiles/([A-Za-z0-9_-]+)/youtube-channels/([A-Za-z0-9_-]+))", [&](const httplib::Request& req, httplib::Response& res) {
        cors(res); std::lock_guard<std::mutex> lock(dbMutex); const std::string profileId=req.matches[1].str(),channelId=req.matches[2].str(); sqlite3_stmt* check=nullptr; if(sqlite3_prepare_v2(db,"SELECT locked FROM profiles WHERE id=?",-1,&check,nullptr)!=SQLITE_OK){sendJson(res,errorJson("Database prepare failed"),500);return;}sqlite3_bind_text(check,1,profileId.c_str(),-1,SQLITE_TRANSIENT);bool locked=sqlite3_step(check)==SQLITE_ROW&&sqlite3_column_int(check,0)!=0;sqlite3_finalize(check);if(locked){sendJson(res,errorJson("Profil jest zablokowany. Odblokuj go przed edycją kanałów."),409);return;}
        sqlite3_stmt* stmt=nullptr;if(sqlite3_prepare_v2(db,"DELETE FROM profile_youtube_channels WHERE profile_id=? AND id=?",-1,&stmt,nullptr)!=SQLITE_OK){sendJson(res,errorJson("Database prepare failed"),500);return;}sqlite3_bind_text(stmt,1,profileId.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(stmt,2,channelId.c_str(),-1,SQLITE_TRANSIENT);int rc=sqlite3_step(stmt),changed=sqlite3_changes(db);sqlite3_finalize(stmt);if(rc!=SQLITE_DONE){sendJson(res,errorJson("Database delete failed"),500);return;}if(!changed){sendJson(res,errorJson("Kanał nie istnieje."),404);return;}sendJson(res,{{"ok",true}});
    });

    // Subjects: the first persistent slice of the academic domain model.
    server.Get("/api/subjects", [&](const httplib::Request&, httplib::Response& res) {
        cors(res);
        std::lock_guard<std::mutex> lock(dbMutex);
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, "SELECT id,name,color,is_lecture,notes,created_at,updated_at FROM subjects ORDER BY name COLLATE NOCASE", -1, &stmt, nullptr) != SQLITE_OK) {
            sendJson(res, errorJson("Database read failed"), 500); return;
        }
        json subjects = json::array();
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            auto col = [&](int i) { const unsigned char* p = sqlite3_column_text(stmt, i); return p ? std::string(reinterpret_cast<const char*>(p)) : std::string(); };
            subjects.push_back({{"id",col(0)},{"name",col(1)},{"color",col(2)},{"isLecture",sqlite3_column_int(stmt,3)!=0},{"notes",col(4)},{"createdAt",col(5)},{"updatedAt",col(6)}});
        }
        sqlite3_finalize(stmt); sendJson(res, {{"subjects",subjects}});
    });
    server.Post("/api/subjects", [&](const httplib::Request& req, httplib::Response& res) {
        cors(res); json body;
        try { body = json::parse(req.body); } catch (...) { sendJson(res, errorJson("Invalid JSON body"), 400); return; }
        if (!body.is_object() || !body.contains("name") || !body["name"].is_string()) {
            sendJson(res, errorJson("Field 'name' must be a string"), 400); return;
        }
        std::string name = body["name"].get<std::string>();
        while (!name.empty() && std::isspace(static_cast<unsigned char>(name.front()))) name.erase(name.begin());
        while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back()))) name.pop_back();
        std::string color = body.contains("color") && body["color"].is_string() ? body["color"].get<std::string>() : "#8aa66a";
        if (name.empty() || name.size() > 120) { sendJson(res, errorJson("Nazwa przedmiotu musi mieć od 1 do 120 znaków."), 400); return; }
        bool validColor = color.size() == 7 && color[0] == '#';
        for (size_t i=1; validColor && i<color.size(); ++i) validColor = std::isxdigit(static_cast<unsigned char>(color[i])) != 0;
        if (!validColor) { sendJson(res, errorJson("Nieprawidłowy kolor przedmiotu."), 400); return; }
        std::string id = "subject-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        std::lock_guard<std::mutex> lock(dbMutex); sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, "INSERT INTO subjects(id,name,color,is_lecture) VALUES(?,?,?,1)", -1, &stmt, nullptr) != SQLITE_OK) { sendJson(res, errorJson("Database prepare failed"), 500); return; }
        sqlite3_bind_text(stmt,1,id.c_str(),-1,SQLITE_TRANSIENT); sqlite3_bind_text(stmt,2,name.c_str(),-1,SQLITE_TRANSIENT); sqlite3_bind_text(stmt,3,color.c_str(),-1,SQLITE_TRANSIENT);
        int rc = sqlite3_step(stmt); sqlite3_finalize(stmt);
        if (rc == SQLITE_CONSTRAINT) { sendJson(res, errorJson("Przedmiot o tej nazwie już istnieje."), 409); return; }
        if (rc != SQLITE_DONE) { sendJson(res, errorJson("Database write failed"), 500); return; }
        sendJson(res, {{"ok",true},{"subject",{{"id",id},{"name",name},{"color",color},{"isLecture",true},{"notes",""}}}}, 201);
    });
    server.Put(R"(/api/subjects/([A-Za-z0-9_-]+))", [&](const httplib::Request& req, httplib::Response& res) {
        cors(res); json body;
        try { body = json::parse(req.body); } catch (...) { sendJson(res,errorJson("Invalid JSON body"),400); return; }
        if (!body.is_object() || !body.contains("name") || !body["name"].is_string() || !body.contains("color") || !body["color"].is_string()) { sendJson(res,errorJson("Wymagane pola name i color."),400); return; }
        std::string name=body["name"].get<std::string>(), color=body["color"].get<std::string>();
        while(!name.empty() && std::isspace(static_cast<unsigned char>(name.front()))) name.erase(name.begin());
        while(!name.empty() && std::isspace(static_cast<unsigned char>(name.back()))) name.pop_back();
        bool validColor=color.size()==7 && color[0]=='#'; for(size_t i=1;validColor&&i<color.size();++i) validColor=std::isxdigit(static_cast<unsigned char>(color[i]))!=0;
        if(name.empty()||name.size()>120||!validColor){sendJson(res,errorJson("Sprawdź nazwę i kolor przedmiotu."),400);return;}
        std::lock_guard<std::mutex> lock(dbMutex); sqlite3_stmt* stmt=nullptr;
        if(sqlite3_prepare_v2(db,"UPDATE subjects SET name=?,color=?,updated_at=CURRENT_TIMESTAMP WHERE id=?",-1,&stmt,nullptr)!=SQLITE_OK){sendJson(res,errorJson("Database prepare failed"),500);return;}
        const std::string id=req.matches[1].str(); sqlite3_bind_text(stmt,1,name.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(stmt,2,color.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(stmt,3,id.c_str(),-1,SQLITE_TRANSIENT);
        int rc=sqlite3_step(stmt),changed=sqlite3_changes(db);sqlite3_finalize(stmt);
        if(rc==SQLITE_CONSTRAINT){sendJson(res,errorJson("Przedmiot o tej nazwie już istnieje."),409);return;} if(rc!=SQLITE_DONE){sendJson(res,errorJson("Database update failed"),500);return;} if(!changed){sendJson(res,errorJson("Subject not found"),404);return;}
        sendJson(res,{{"ok",true},{"subject",{{"id",id},{"name",name},{"color",color}}}});
    });
    server.Delete(R"(/api/subjects/([A-Za-z0-9_-]+))", [&](const httplib::Request& req, httplib::Response& res) {
        cors(res); std::lock_guard<std::mutex> lock(dbMutex); sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, "DELETE FROM subjects WHERE id=?", -1, &stmt, nullptr) != SQLITE_OK) { sendJson(res, errorJson("Database prepare failed"), 500); return; }
        std::string id = req.matches[1].str(); sqlite3_bind_text(stmt,1,id.c_str(),-1,SQLITE_TRANSIENT); int rc=sqlite3_step(stmt); int changed=sqlite3_changes(db); sqlite3_finalize(stmt);
        if (rc != SQLITE_DONE) { sendJson(res, errorJson("Database delete failed"), 500); return; }
        if (!changed) { sendJson(res, errorJson("Subject not found"), 404); return; }
        sendJson(res, {{"ok",true}});
    });

    // Recurring weekly timetable entries associated with a subject.
    server.Get("/api/sessions", [&](const httplib::Request&, httplib::Response& res) {
        cors(res); std::lock_guard<std::mutex> lock(dbMutex); sqlite3_stmt* stmt=nullptr;
        const char* sql="SELECT ss.id,ss.subject_id,s.name,s.color,ss.weekday,ss.start_time,ss.end_time,ss.room,ss.session_type FROM subject_sessions ss JOIN subjects s ON s.id=ss.subject_id ORDER BY ss.weekday,ss.start_time";
        if(sqlite3_prepare_v2(db,sql,-1,&stmt,nullptr)!=SQLITE_OK){sendJson(res,errorJson("Nie udało się odczytać planu zajęć."),500);return;}
        auto col=[](sqlite3_stmt* q,int i){const unsigned char* p=sqlite3_column_text(q,i);return p?std::string(reinterpret_cast<const char*>(p)):std::string();}; json rows=json::array();
        while(sqlite3_step(stmt)==SQLITE_ROW) rows.push_back({{"id",col(stmt,0)},{"subjectId",col(stmt,1)},{"subjectName",col(stmt,2)},{"color",col(stmt,3)},{"weekday",sqlite3_column_int(stmt,4)},{"startTime",col(stmt,5)},{"endTime",col(stmt,6)},{"room",col(stmt,7)},{"sessionType",col(stmt,8)}});
        sqlite3_finalize(stmt); sendJson(res,{{"sessions",rows}});
    });
    server.Post("/api/sessions", [&](const httplib::Request& req, httplib::Response& res) {
        cors(res); json body; try{body=json::parse(req.body);}catch(...){sendJson(res,errorJson("Nieprawidłowy JSON."),400);return;}
        if(!body.is_object()||!body.contains("subjectId")||!body["subjectId"].is_string()||!body.contains("weekday")||!body["weekday"].is_number_integer()||!body.contains("startTime")||!body["startTime"].is_string()||!body.contains("endTime")||!body["endTime"].is_string()) {sendJson(res,errorJson("Wybierz przedmiot, dzień i godziny."),400);return;}
        const std::string subjectId=body["subjectId"].get<std::string>(), start=body["startTime"].get<std::string>(), end=body["endTime"].get<std::string>(); int weekday=body["weekday"].get<int>();
        auto validTime=[](const std::string& t){return t.size()==5&&t[2]==':'&&std::isdigit(static_cast<unsigned char>(t[0]))&&std::isdigit(static_cast<unsigned char>(t[1]))&&std::isdigit(static_cast<unsigned char>(t[3]))&&std::isdigit(static_cast<unsigned char>(t[4]))&&std::stoi(t.substr(0,2))<24&&std::stoi(t.substr(3,2))<60;};
        const std::string room=body.value("room",std::string()), type=body.value("sessionType",std::string("Wykład"));
        if(weekday<0||weekday>6||!validTime(start)||!validTime(end)||end<=start||room.size()>120||type.empty()||type.size()>40){sendJson(res,errorJson("Sprawdź dzień, godziny, salę i typ zajęć."),400);return;}
        const std::string id="session-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()); std::lock_guard<std::mutex> lock(dbMutex); sqlite3_stmt* stmt=nullptr;
        if(sqlite3_prepare_v2(db,"INSERT INTO subject_sessions(id,subject_id,weekday,start_time,end_time,room,session_type) VALUES(?,?,?,?,?,?,?)",-1,&stmt,nullptr)!=SQLITE_OK){sendJson(res,errorJson("Nie udało się przygotować zapisu."),500);return;}
        sqlite3_bind_text(stmt,1,id.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(stmt,2,subjectId.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_int(stmt,3,weekday);sqlite3_bind_text(stmt,4,start.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(stmt,5,end.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(stmt,6,room.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(stmt,7,type.c_str(),-1,SQLITE_TRANSIENT);
        int rc=sqlite3_step(stmt);sqlite3_finalize(stmt);if(rc==SQLITE_CONSTRAINT_FOREIGNKEY){sendJson(res,errorJson("Wybrany przedmiot już nie istnieje."),404);return;}if(rc!=SQLITE_DONE){sendJson(res,errorJson("Nie udało się zapisać zajęć."),500);return;}sendJson(res,{{"ok",true},{"id",id}},201);
    });
    server.Put(R"(/api/sessions/([A-Za-z0-9_-]+))", [&](const httplib::Request& req, httplib::Response& res) {
        cors(res); json body; try { body=json::parse(req.body); } catch (...) { sendJson(res,errorJson("Nieprawidłowy JSON."),400); return; }
        if(!body.is_object()||!body.contains("subjectId")||!body["subjectId"].is_string()||!body.contains("weekday")||!body["weekday"].is_number_integer()||!body.contains("startTime")||!body["startTime"].is_string()||!body.contains("endTime")||!body["endTime"].is_string()) { sendJson(res,errorJson("Wybierz przedmiot, dzień i godziny."),400); return; }
        const std::string id=req.matches[1].str(), subjectId=body["subjectId"].get<std::string>(), start=body["startTime"].get<std::string>(), end=body["endTime"].get<std::string>(); const int weekday=body["weekday"].get<int>();
        auto validTime=[](const std::string& t){return t.size()==5&&t[2]==':'&&std::isdigit(static_cast<unsigned char>(t[0]))&&std::isdigit(static_cast<unsigned char>(t[1]))&&std::isdigit(static_cast<unsigned char>(t[3]))&&std::isdigit(static_cast<unsigned char>(t[4]))&&std::stoi(t.substr(0,2))<24&&std::stoi(t.substr(3,2))<60;};
        const std::string room=body.value("room",std::string()), type=body.value("sessionType",std::string("Wykład"));
        if(weekday<0||weekday>6||!validTime(start)||!validTime(end)||end<=start||room.size()>120||type.empty()||type.size()>40){sendJson(res,errorJson("Sprawdź dzień, godziny, salę i typ zajęć."),400);return;}
        std::lock_guard<std::mutex> lock(dbMutex); sqlite3_stmt* stmt=nullptr;
        const char* sql="UPDATE subject_sessions SET subject_id=?,weekday=?,start_time=?,end_time=?,room=?,session_type=? WHERE id=?";
        if(sqlite3_prepare_v2(db,sql,-1,&stmt,nullptr)!=SQLITE_OK){sendJson(res,errorJson("Nie udało się przygotować aktualizacji."),500);return;}
        sqlite3_bind_text(stmt,1,subjectId.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_int(stmt,2,weekday);sqlite3_bind_text(stmt,3,start.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(stmt,4,end.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(stmt,5,room.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(stmt,6,type.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(stmt,7,id.c_str(),-1,SQLITE_TRANSIENT);
        int rc=sqlite3_step(stmt), changed=sqlite3_changes(db);sqlite3_finalize(stmt);if(rc==SQLITE_CONSTRAINT_FOREIGNKEY){sendJson(res,errorJson("Wybrany przedmiot już nie istnieje."),404);return;}if(rc!=SQLITE_DONE){sendJson(res,errorJson("Nie udało się zaktualizować zajęć."),500);return;}if(!changed){sendJson(res,errorJson("Nie znaleziono zajęć."),404);return;}sendJson(res,{{"ok",true}});
    });
    server.Delete(R"(/api/sessions/([A-Za-z0-9_-]+))", [&](const httplib::Request& req, httplib::Response& res) {
        cors(res);std::lock_guard<std::mutex> lock(dbMutex);sqlite3_stmt* stmt=nullptr;if(sqlite3_prepare_v2(db,"DELETE FROM subject_sessions WHERE id=?",-1,&stmt,nullptr)!=SQLITE_OK){sendJson(res,errorJson("Nie udało się przygotować usuwania."),500);return;}const std::string id=req.matches[1].str();sqlite3_bind_text(stmt,1,id.c_str(),-1,SQLITE_TRANSIENT);int rc=sqlite3_step(stmt),changed=sqlite3_changes(db);sqlite3_finalize(stmt);if(rc!=SQLITE_DONE){sendJson(res,errorJson("Nie udało się usunąć zajęć."),500);return;}if(!changed){sendJson(res,errorJson("Nie znaleziono zajęć."),404);return;}sendJson(res,{{"ok",true}});
    });

    // Materialize the recurring weekly timetable into calendar events for one week.
    // Event IDs are deterministic, so repeating this operation updates existing
    // generated events instead of creating duplicates.
    server.Post("/api/sessions/generate-events", [&](const httplib::Request& req, httplib::Response& res) {
        cors(res);
        json body;
        try { body = json::parse(req.body); }
        catch (...) { sendJson(res, errorJson("Nieprawidłowy JSON."), 400); return; }
        if (!body.is_object() || !body.contains("weekStart") || !body["weekStart"].is_string()) {
            sendJson(res, errorJson("Podaj datę poniedziałku w polu weekStart (YYYY-MM-DD)."), 400); return;
        }
        const std::string weekStart = body["weekStart"].get<std::string>();
        int year=0, month=0, day=0;
        if (weekStart.size()!=10 || std::sscanf(weekStart.c_str(), "%4d-%2d-%2d", &year, &month, &day)!=3) {
            sendJson(res, errorJson("Data tygodnia musi mieć format YYYY-MM-DD."), 400); return;
        }
        std::tm monday{}; monday.tm_year=year-1900; monday.tm_mon=month-1; monday.tm_mday=day; monday.tm_hour=12; monday.tm_isdst=-1;
        const std::time_t mondayTime=std::mktime(&monday);
        if (mondayTime == static_cast<std::time_t>(-1)) { sendJson(res,errorJson("Nieprawidłowa data tygodnia."),400); return; }
        std::tm normalized{};
#ifdef _WIN32
        localtime_s(&normalized,&mondayTime);
#else
        localtime_r(&mondayTime,&normalized);
#endif
        char normalizedDate[11]{}; std::strftime(normalizedDate,sizeof(normalizedDate),"%Y-%m-%d",&normalized);
        if (weekStart != normalizedDate || (normalized.tm_wday+6)%7 != 0) {
            sendJson(res,errorJson("Wybierz prawidłową datę poniedziałku."),400); return;
        }
        std::lock_guard<std::mutex> lock(dbMutex);
        sqlite3_stmt* stmt=nullptr;
        const char* selectSql="SELECT ss.id,s.name,ss.weekday,ss.start_time,ss.end_time,ss.room,ss.session_type FROM subject_sessions ss JOIN subjects s ON s.id=ss.subject_id ORDER BY ss.weekday,ss.start_time";
        if(sqlite3_prepare_v2(db,selectSql,-1,&stmt,nullptr)!=SQLITE_OK){sendJson(res,errorJson("Nie udało się odczytać tygodniowego planu."),500);return;}
        struct WeeklyRow { std::string id,name,start,end,room,type; int weekday; };
        std::vector<WeeklyRow> rows;
        auto col=[](sqlite3_stmt* q,int i){const unsigned char* p=sqlite3_column_text(q,i);return p?std::string(reinterpret_cast<const char*>(p)):std::string();};
        while(sqlite3_step(stmt)==SQLITE_ROW) rows.push_back({col(stmt,0),col(stmt,1),col(stmt,3),col(stmt,4),col(stmt,5),col(stmt,6),sqlite3_column_int(stmt,2)});
        sqlite3_finalize(stmt);
        if(!execSql("BEGIN IMMEDIATE;")){sendJson(res,errorJson("Nie udało się rozpocząć transakcji."),500);return;}
        int generated=0, updated=0;
        for(const auto& row:rows){
            std::tm eventDate{}; eventDate.tm_year=year-1900; eventDate.tm_mon=month-1; eventDate.tm_mday=day+row.weekday; eventDate.tm_hour=12; eventDate.tm_isdst=-1;
            std::time_t eventTime=std::mktime(&eventDate); std::tm local{};
            if(eventTime==static_cast<std::time_t>(-1)) { execSql("ROLLBACK;"); sendJson(res,errorJson("Nie udało się obliczyć dat wydarzeń."),500); return; }
#ifdef _WIN32
            localtime_s(&local,&eventTime);
#else
            localtime_r(&eventTime,&local);
#endif
            char date[11]{}; std::strftime(date,sizeof(date),"%Y-%m-%d",&local);
            const std::string eventId="schedule_"+row.id+"_"+std::string(date);
            const std::string title=row.name+" — "+row.type+(row.room.empty()?std::string():" · "+row.room);
            const std::string start=std::string(date)+"T"+row.start+":00";
            const std::string end=std::string(date)+"T"+row.end+":00";
            std::string eventType="lecture";
            if(row.type=="Laboratorium" || row.type=="Laboratoria") eventType="lab";
            else if(row.type=="Ćwiczenia" || row.type=="Cwiczenia") eventType="exercise";
            else if(row.type=="Projekt") eventType="project";
            else if(row.type=="Kolokwium" || row.type=="Kolokwium na wykładzie" || row.type=="Kolokwium na ćwiczeniach") eventType="examLecture";
            sqlite3_stmt* exists=nullptr;
            if(sqlite3_prepare_v2(db,"SELECT 1 FROM calendar_events WHERE id=?",-1,&exists,nullptr)!=SQLITE_OK){execSql("ROLLBACK;");sendJson(res,errorJson("Nie udało się sprawdzić wydarzeń."),500);return;}
            sqlite3_bind_text(exists,1,eventId.c_str(),-1,SQLITE_TRANSIENT); bool wasExisting=sqlite3_step(exists)==SQLITE_ROW; sqlite3_finalize(exists);
            sqlite3_stmt* write=nullptr;
            const char* upsert="INSERT INTO calendar_events(id,title,start_at,end_at,event_type,updated_at) VALUES(?,?,?,?,?,CURRENT_TIMESTAMP) ON CONFLICT(id) DO UPDATE SET title=excluded.title,start_at=excluded.start_at,end_at=excluded.end_at,event_type=excluded.event_type,updated_at=CURRENT_TIMESTAMP";
            if(sqlite3_prepare_v2(db,upsert,-1,&write,nullptr)!=SQLITE_OK){execSql("ROLLBACK;");sendJson(res,errorJson("Nie udało się przygotować wydarzeń."),500);return;}
            sqlite3_bind_text(write,1,eventId.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(write,2,title.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(write,3,start.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(write,4,end.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(write,5,eventType.c_str(),-1,SQLITE_TRANSIENT);
            int rc=sqlite3_step(write);sqlite3_finalize(write);if(rc!=SQLITE_DONE){execSql("ROLLBACK;");sendJson(res,errorJson("Nie udało się zapisać wydarzeń."),500);return;}
            if(wasExisting) ++updated; else ++generated;
        }
        if(!execSql("COMMIT;")){execSql("ROLLBACK;");sendJson(res,errorJson("Nie udało się zatwierdzić wydarzeń."),500);return;}
        sendJson(res,{{"ok",true},{"weekStart",weekStart},{"generated",generated},{"updated",updated},{"total",rows.size()}});
    });

    server.Get("/api/google/status", [&](const httplib::Request&, httplib::Response& res) { cors(res); sendJson(res, googleStatus()); });
    server.Get("/api/google/connect", [&](const httplib::Request&, httplib::Response& res) {
        cors(res); GoogleCredentials c;
        if(!loadGoogleCredentials(c)){sendJson(res,{{"error","Nie znaleziono poprawnego Library/credentials.json. Utwórz OAuth Client ID typu Desktop/Web w Google Cloud Console."},{"status",googleStatus()}},400);return;}
        oauthState=randomHex(48); const std::string redirect="http://127.0.0.1:43127";
        std::string url=c.authUri+"?client_id="+urlEncode(c.clientId)+"&redirect_uri="+urlEncode(redirect)+"&response_type=code&scope="+urlEncode("https://www.googleapis.com/auth/calendar.events")+"&access_type=offline&prompt=consent&include_granted_scopes=true&state="+oauthState;
        sendJson(res,{{"authUrl",url},{"redirectUri",redirect}});
    });
    server.Get("/", [&](const httplib::Request& req, httplib::Response& res) {
        std::string state=req.get_param_value("state"), code=req.get_param_value("code"), error=req.get_param_value("error");
        if(!error.empty()){res.status=400;res.set_content("<html><meta charset='utf-8'><h2>Nie udało się połączyć Google Calendar</h2><p>Autoryzacja została anulowana lub odrzucona przez Google.</p><p>Możesz zamknąć to okno.</p></html>","text/html; charset=utf-8");return;}
        if(state.empty()||state!=oauthState||code.empty()){res.status=400;res.set_content("<html><meta charset='utf-8'><h2>Nieprawidłowa odpowiedź OAuth</h2><p>Stan autoryzacji nie pasuje. Wróć do Amogusa i spróbuj ponownie.</p></html>","text/html; charset=utf-8");return;}
        GoogleCredentials c;if(!loadGoogleCredentials(c)){res.status=500;res.set_content("Brak credentials.json.","text/plain; charset=utf-8");return;}
        const std::string redirect="http://127.0.0.1:43127";
        std::string body="client_id="+urlEncode(c.clientId)+"&client_secret="+urlEncode(c.clientSecret)+"&code="+urlEncode(code)+"&grant_type=authorization_code&redirect_uri="+urlEncode(redirect);
        std::string response=curlRequest("POST",c.tokenUri,{"Content-Type: application/x-www-form-urlencoded"},body);json token;
        try{token=json::parse(response);}catch(...){token=json::object();}
        if(!token.contains("refresh_token")||!token.contains("access_token")){res.status=502;res.set_content("<html><meta charset='utf-8'><h2>Google OAuth nie powiódł się</h2><p>Nie otrzymano tokenu odświeżania. Sprawdź redirect URI, typ klienta OAuth, consent screen i połączenie sieciowe.</p><p>Możesz zamknąć to okno i wrócić do Amogusa.</p></html>","text/html; charset=utf-8");return;}
        token["expires_at"]=std::time(nullptr)+token.value("expires_in",3600)-60;
        if(!writeJsonFile(googleTokenPath(),token)){res.status=500;res.set_content("Nie udało się zapisać tokenu w lokalnym katalogu danych Amogusa.","text/plain; charset=utf-8");return;}
        oauthState.clear(); googleLastError.clear();
        res.set_content("<html><meta charset='utf-8'><title>Google Calendar połączony</title><body style='font:16px system-ui;max-width:620px;margin:60px auto'><h2>Google Calendar połączony ✓</h2><p>Autoryzacja zakończona. Wróć do Amogusa i kliknij „Synchronizuj teraz”.</p><script>setTimeout(()=>window.close(),1200)</script></body></html>","text/html; charset=utf-8");
    });
    server.Post("/api/google/sync", [&](const httplib::Request&, httplib::Response& res) {
        cors(res); std::lock_guard<std::mutex> syncLock(googleSyncMutex);
        GoogleCredentials c;if(!loadGoogleCredentials(c)){googleLastError="Brak Library/credentials.json";sendJson(res,{{"error",googleLastError},{"status",googleStatus()}},400);return;}
        json token;googleLastError.clear();std::string access=getAccessToken(c,token);if(access.empty()){googleLastError="Brak ważnej autoryzacji. Połącz konto Google ponownie.";sendJson(res,{{"error",googleLastError},{"status",googleStatus()}},401);return;}
        const std::string auth="Authorization: Bearer "+access;
        json remote=json::array();std::string pageToken;bool fetchOk=true;
        for(int page=0;page<40;++page){std::string url="https://www.googleapis.com/calendar/v3/calendars/primary/events?maxResults=250&showDeleted=true&singleEvents=true&orderBy=startTime";if(!pageToken.empty())url+="&pageToken="+urlEncode(pageToken);std::string raw=curlRequest("GET",url,{auth});json data;try{data=json::parse(raw);}catch(...){fetchOk=false;googleLastError="Nie udało się odczytać odpowiedzi Google Calendar.";break;}if(data.contains("error")){fetchOk=false;googleLastError=data["error"].value("message",std::string("Błąd Google Calendar API"));break;}if(data.contains("items")&&data["items"].is_array())for(auto&e:data["items"])remote.push_back(e);pageToken=data.value("nextPageToken",std::string());if(pageToken.empty())break;}
        if(!fetchOk){sendJson(res,{{"error",googleLastError},{"status",googleStatus()}},502);return;}
        // Local deletions are retained as tombstones until Google confirms deletion.
        std::vector<std::string> tombstones;{std::lock_guard<std::mutex> lock(dbMutex);sqlite3_stmt* st=nullptr;if(sqlite3_prepare_v2(db,"SELECT google_id FROM google_deleted_events",-1,&st,nullptr)==SQLITE_OK){while(sqlite3_step(st)==SQLITE_ROW){const unsigned char*x=sqlite3_column_text(st,0);if(x)tombstones.emplace_back(reinterpret_cast<const char*>(x));}sqlite3_finalize(st);}}
        for(const auto& gid:tombstones){std::string raw=curlRequest("DELETE","https://www.googleapis.com/calendar/v3/calendars/primary/events/"+urlEncode(gid),{auth});if(raw.empty()||raw.find("404")!=std::string::npos||!googleError(raw)){std::lock_guard<std::mutex> lock(dbMutex);sqlite3_stmt* st=nullptr;sqlite3_prepare_v2(db,"DELETE FROM google_deleted_events WHERE google_id=?",-1,&st,nullptr);sqlite3_bind_text(st,1,gid.c_str(),-1,SQLITE_TRANSIENT);sqlite3_step(st);sqlite3_finalize(st);}}std::set<std::string> pendingDeletionIds(tombstones.begin(),tombstones.end());
        struct LocalEvent{std::string id,title,start,end,updated,gid,gupdated,hash;};std::vector<LocalEvent> local;
        {std::lock_guard<std::mutex> lock(dbMutex);sqlite3_stmt* st=nullptr;if(sqlite3_prepare_v2(db,"SELECT id,title,start_at,end_at,updated_at,google_id,google_updated_at,synced_hash FROM calendar_events ORDER BY start_at",-1,&st,nullptr)!=SQLITE_OK){sendJson(res,errorJson("Nie udało się odczytać lokalnych wydarzeń."),500);return;}while(sqlite3_step(st)==SQLITE_ROW){auto col=[&](int i){const unsigned char*x=sqlite3_column_text(st,i);return x?std::string(reinterpret_cast<const char*>(x)):std::string();};local.push_back({col(0),col(1),col(2),col(3),col(4),col(5),col(6),col(7)});}sqlite3_finalize(st);}
        std::map<std::string,json> remoteById;std::map<std::string,json> remoteByLocalId;std::map<std::string,std::string> localByGoogleId;for(auto&e:remote)if(e.is_object()&&e.contains("id")){remoteById[e.value("id",std::string())]=e;try{std::string lid=e["extendedProperties"]["private"].value("amogusLocalId",std::string());if(!lid.empty())remoteByLocalId[lid]=e;}catch(...){}}for(auto&e:local)if(!e.gid.empty())localByGoogleId[e.gid]=e.id;
        int uploaded=0,downloaded=0,updated=0,deleted=0;std::set<std::string> processedRemote;
        // Process local events first. If the local content changed since the last sync, it wins this round.
        for(auto&e:local){std::string hash=eventHash(e.title,e.start,e.end);if(e.gid.empty()){auto byLocal=remoteByLocalId.find(e.id);if(byLocal!=remoteByLocalId.end()){e.gid=byLocal->second.value("id",std::string());e.gupdated=byLocal->second.value("updated",std::string());std::lock_guard<std::mutex> lock(dbMutex);sqlite3_stmt* st=nullptr;sqlite3_prepare_v2(db,"UPDATE calendar_events SET google_id=?,google_updated_at=? WHERE id=?",-1,&st,nullptr);sqlite3_bind_text(st,1,e.gid.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(st,2,e.gupdated.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(st,3,e.id.c_str(),-1,SQLITE_TRANSIENT);sqlite3_step(st);sqlite3_finalize(st);}}auto it=e.gid.empty()?remoteById.end():remoteById.find(e.gid);
            if(it==remoteById.end()||it->second.value("status",std::string())=="cancelled"){
                if(!e.gid.empty()) {std::lock_guard<std::mutex> lock(dbMutex);sqlite3_stmt* st=nullptr;sqlite3_prepare_v2(db,"DELETE FROM calendar_events WHERE id=?",-1,&st,nullptr);sqlite3_bind_text(st,1,e.id.c_str(),-1,SQLITE_TRANSIENT);sqlite3_step(st);sqlite3_finalize(st);++deleted;}
                else {std::string raw=curlRequest("POST","https://www.googleapis.com/calendar/v3/calendars/primary/events",{auth,"Content-Type: application/json"},googleEventBody(e.id,e.title,e.start,e.end));json created;try{created=json::parse(raw);}catch(...){created=json::object();}if(created.contains("id")){std::string gid=created.value("id",std::string()),gu=created.value("updated",std::string());std::lock_guard<std::mutex> lock(dbMutex);sqlite3_stmt* st=nullptr;sqlite3_prepare_v2(db,"UPDATE calendar_events SET google_id=?,google_updated_at=?,synced_hash=? WHERE id=?",-1,&st,nullptr);sqlite3_bind_text(st,1,gid.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(st,2,gu.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(st,3,hash.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(st,4,e.id.c_str(),-1,SQLITE_TRANSIENT);sqlite3_step(st);sqlite3_finalize(st);processedRemote.insert(gid);++uploaded;}else{googleLastError="Nie udało się wysłać wydarzenia: "+e.title;}}
                continue;
            }
            json r=it->second;processedRemote.insert(e.gid);std::string rs=normalizeDate(r.value("start",json::object())), re=normalizeDate(r.value("end",json::object())), rt=r.value("summary",std::string()), ru=r.value("updated",std::string());
            if(hash!=e.hash || e.gid.empty()) {std::string raw=curlRequest("PATCH","https://www.googleapis.com/calendar/v3/calendars/primary/events/"+urlEncode(e.gid),{auth,"Content-Type: application/json"},googleEventBody(e.id,e.title,e.start,e.end));json patched;try{patched=json::parse(raw);}catch(...){patched=json::object();}if(patched.contains("id")){std::lock_guard<std::mutex> lock(dbMutex);sqlite3_stmt* st=nullptr;sqlite3_prepare_v2(db,"UPDATE calendar_events SET google_updated_at=?,synced_hash=? WHERE id=?",-1,&st,nullptr);sqlite3_bind_text(st,1,patched.value("updated",ru).c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(st,2,hash.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(st,3,e.id.c_str(),-1,SQLITE_TRANSIENT);sqlite3_step(st);sqlite3_finalize(st);++updated;}}
            else if(ru!=e.gupdated && (!rt.empty()||!rs.empty()||!re.empty())) {std::string title=rt.empty()?e.title:rt,start=rs.empty()?e.start:rs,end=re.empty()?e.end:re;std::string nh=eventHash(title,start,end);std::lock_guard<std::mutex> lock(dbMutex);sqlite3_stmt* st=nullptr;sqlite3_prepare_v2(db,"UPDATE calendar_events SET title=?,start_at=?,end_at=?,updated_at=CURRENT_TIMESTAMP,google_updated_at=?,synced_hash=? WHERE id=?",-1,&st,nullptr);sqlite3_bind_text(st,1,title.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(st,2,start.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(st,3,end.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(st,4,ru.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(st,5,nh.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(st,6,e.id.c_str(),-1,SQLITE_TRANSIENT);sqlite3_step(st);sqlite3_finalize(st);++updated;}
        }
        // Import events created in Google Calendar, while ignoring events already mapped to a local row.
        for(auto&r:remote){if(!r.is_object()||!r.contains("id"))continue;std::string gid=r.value("id",std::string());if(gid.empty()||pendingDeletionIds.count(gid)||r.value("status",std::string())=="cancelled"||processedRemote.count(gid)||localByGoogleId.count(gid))continue;
            std::string title=r.value("summary",std::string("(Bez tytułu)")),start=normalizeDate(r.value("start",json::object())),end=normalizeDate(r.value("end",json::object()));if(start.empty()||end.empty())continue;
            std::string id="gcal-"+gid;for(char&ch:id)if(!(std::isalnum((unsigned char)ch)||ch=='-'||ch=='_'))ch='-';std::string ru=r.value("updated",std::string()),hash=eventHash(title,start,end);
            std::lock_guard<std::mutex> lock(dbMutex);sqlite3_stmt* st=nullptr;sqlite3_prepare_v2(db,"INSERT INTO calendar_events(id,title,start_at,end_at,google_id,google_updated_at,synced_hash) VALUES(?,?,?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET title=excluded.title,start_at=excluded.start_at,end_at=excluded.end_at,google_id=excluded.google_id,google_updated_at=excluded.google_updated_at,synced_hash=excluded.synced_hash",-1,&st,nullptr);sqlite3_bind_text(st,1,id.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(st,2,title.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(st,3,start.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(st,4,end.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(st,5,gid.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(st,6,ru.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(st,7,hash.c_str(),-1,SQLITE_TRANSIENT);sqlite3_step(st);sqlite3_finalize(st);localByGoogleId[gid]=id;++downloaded;
        }
        googleLastSync=std::to_string(std::time(nullptr));
        sendJson(res,{{"ok",true},{"uploaded",uploaded},{"downloaded",downloaded},{"updated",updated},{"deleted",deleted},{"remoteCount",remote.size()},{"status",googleStatus()}});
    });

    server.Get("/api/events", [&](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dbMutex); cors(res);
        json events = json::array(); sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, "SELECT id,title,start_at,end_at,event_type FROM calendar_events ORDER BY start_at", -1, &stmt, nullptr) != SQLITE_OK) {
            sendJson(res, errorJson("Database read failed"), 500); return;
        }
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            auto col = [&](int i) { const unsigned char* p = sqlite3_column_text(stmt, i); return p ? std::string(reinterpret_cast<const char*>(p)) : std::string(); };
            events.push_back({{"id", col(0)}, {"title", col(1)}, {"start", col(2)}, {"end", col(3)}, {"eventType", col(4)}});
        }
        sqlite3_finalize(stmt); sendJson(res, {{"events", events}});
    });
    server.Post("/api/events", [&](const httplib::Request& req, httplib::Response& res) {
        cors(res); json body;
        try { body = json::parse(req.body); } catch (...) { sendJson(res, errorJson("Invalid JSON body"), 400); return; }
        if (!body.is_object() || !body.contains("id") || !body["id"].is_string() || !body.contains("title") || !body["title"].is_string() || !body.contains("start") || !body["start"].is_string() || !body.contains("end") || !body["end"].is_string()) {
            sendJson(res, errorJson("Fields id, title, start and end must be strings"), 400); return;
        }
        std::string id = body["id"].get<std::string>(), title = body["title"].get<std::string>();
        std::string start = body["start"].get<std::string>(), end = body["end"].get<std::string>();
        std::string eventType = body.value("eventType", std::string("lecture"));
        const std::vector<std::string> allowedTypes = {"lab","lecture","exercise","project","examLecture","examExercise","gym","ownProject","ownEvent"};
        if (std::find(allowedTypes.begin(), allowedTypes.end(), eventType) == allowedTypes.end()) eventType = "lecture";
        if (id.empty() || id.size() > 120 || title.empty() || title.size() > 200 || start.empty() || start.size() > 40 || end.empty() || end.size() > 40) {
            sendJson(res, errorJson("Event fields are empty or too long"), 400); return;
        }
        std::lock_guard<std::mutex> lock(dbMutex); sqlite3_stmt* stmt = nullptr;
        const char* sql = "INSERT INTO calendar_events(id,title,start_at,end_at,event_type,updated_at) VALUES(?,?,?,?,?,CURRENT_TIMESTAMP) ON CONFLICT(id) DO UPDATE SET title=excluded.title,start_at=excluded.start_at,end_at=excluded.end_at,event_type=excluded.event_type,updated_at=CURRENT_TIMESTAMP";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) { sendJson(res, errorJson("Database prepare failed"), 500); return; }
        sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT); sqlite3_bind_text(stmt, 2, title.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, start.c_str(), -1, SQLITE_TRANSIENT); sqlite3_bind_text(stmt, 4, end.c_str(), -1, SQLITE_TRANSIENT); sqlite3_bind_text(stmt, 5, eventType.c_str(), -1, SQLITE_TRANSIENT);
        int rc = sqlite3_step(stmt); sqlite3_finalize(stmt);
        if (rc != SQLITE_DONE) { sendJson(res, errorJson("Database write failed"), 500); return; }
        sendJson(res, {{"ok", true}, {"event", {{"id",id},{"title",title},{"start",start},{"end",end},{"eventType",eventType}}}}, 201);
    });
    server.Delete(R"(/api/events/([A-Za-z0-9_-]+))", [&](const httplib::Request& req, httplib::Response& res) {
        cors(res); std::lock_guard<std::mutex> lock(dbMutex); sqlite3_stmt* stmt = nullptr;
        const std::string deletingId=req.matches[1].str();
        if(sqlite3_prepare_v2(db,"INSERT OR IGNORE INTO google_deleted_events(google_id) SELECT google_id FROM calendar_events WHERE id=? AND google_id<>''",-1,&stmt,nullptr)==SQLITE_OK){sqlite3_bind_text(stmt,1,deletingId.c_str(),-1,SQLITE_TRANSIENT);sqlite3_step(stmt);sqlite3_finalize(stmt);stmt=nullptr;}
        if (sqlite3_prepare_v2(db, "DELETE FROM calendar_events WHERE id=?", -1, &stmt, nullptr) != SQLITE_OK) { sendJson(res, errorJson("Database prepare failed"), 500); return; }
        sqlite3_bind_text(stmt, 1, req.matches[1].str().c_str(), -1, SQLITE_TRANSIENT); int rc = sqlite3_step(stmt); sqlite3_finalize(stmt);
        if (rc != SQLITE_DONE) { sendJson(res, errorJson("Database delete failed"), 500); return; }
        sendJson(res, {{"ok", true}});
    });

    // Overdue tracker: tasks persist locally and can be completed or removed.
    server.Get("/api/overdue", [&](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(dbMutex); cors(res);
        json tasks = json::array(); sqlite3_stmt* stmt = nullptr;
        const char* sql = "SELECT id,title,due_at,notes,completed,priority,category,created_at FROM overdue_tasks ORDER BY completed ASC,CASE priority WHEN 'urgent' THEN 0 WHEN 'high' THEN 1 WHEN 'normal' THEN 2 ELSE 3 END ASC,due_at ASC,created_at ASC";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) { sendJson(res, errorJson("Database read failed"), 500); return; }
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            auto col = [&](int i) { const unsigned char* p = sqlite3_column_text(stmt, i); return p ? std::string(reinterpret_cast<const char*>(p)) : std::string(); };
            tasks.push_back({{"id",col(0)},{"title",col(1)},{"dueAt",col(2)},{"notes",col(3)},{"completed",sqlite3_column_int(stmt,4)!=0},{"priority",col(5)},{"category",col(6)},{"createdAt",col(7)}});
        }
        sqlite3_finalize(stmt); sendJson(res, {{"tasks",tasks}});
    });
    server.Post("/api/overdue", [&](const httplib::Request& req, httplib::Response& res) {
        cors(res); json body;
        try { body = json::parse(req.body); } catch (...) { sendJson(res, errorJson("Invalid JSON body"), 400); return; }
        if (!body.is_object() || !body.contains("title") || !body["title"].is_string() || !body.contains("dueAt") || !body["dueAt"].is_string()) {
            sendJson(res, errorJson("Fields title and dueAt must be strings"), 400); return;
        }
        std::string title = body["title"].get<std::string>(), dueAt = body["dueAt"].get<std::string>();
        if (body.contains("notes") && !body["notes"].is_string()) { sendJson(res, errorJson("Field 'notes' must be a string"), 400); return; }
        std::string notes = body.contains("notes") ? body["notes"].get<std::string>() : std::string();
        std::string priority = body.contains("priority") && body["priority"].is_string() ? body["priority"].get<std::string>() : "normal";
        std::string category = body.contains("category") && body["category"].is_string() ? body["category"].get<std::string>() : "Ogólne";
        if ((priority!="low" && priority!="normal" && priority!="high" && priority!="urgent") || category.empty() || category.size()>80) { sendJson(res,errorJson("Nieprawidłowy priorytet lub kategoria."),400); return; }
        if (title.empty() || title.size() > 200 || dueAt.empty() || dueAt.size() > 40 || notes.size() > 2000) { sendJson(res, errorJson("Title, due date or notes are empty or too long"), 400); return; }
        std::string id = "task-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        std::lock_guard<std::mutex> lock(dbMutex); sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, "INSERT INTO overdue_tasks(id,title,due_at,notes,priority,category) VALUES(?,?,?,?,?,?)", -1, &stmt, nullptr) != SQLITE_OK) { sendJson(res, errorJson("Database prepare failed"), 500); return; }
        sqlite3_bind_text(stmt,1,id.c_str(),-1,SQLITE_TRANSIENT); sqlite3_bind_text(stmt,2,title.c_str(),-1,SQLITE_TRANSIENT); sqlite3_bind_text(stmt,3,dueAt.c_str(),-1,SQLITE_TRANSIENT); sqlite3_bind_text(stmt,4,notes.c_str(),static_cast<int>(notes.size()),SQLITE_TRANSIENT); sqlite3_bind_text(stmt,5,priority.c_str(),-1,SQLITE_TRANSIENT); sqlite3_bind_text(stmt,6,category.c_str(),-1,SQLITE_TRANSIENT);
        int rc = sqlite3_step(stmt); sqlite3_finalize(stmt);
        if (rc != SQLITE_DONE) { sendJson(res, errorJson("Database write failed"), 500); return; }
        sendJson(res, {{"ok",true},{"task",{{"id",id},{"title",title},{"dueAt",dueAt},{"notes",notes},{"completed",false},{"priority",priority},{"category",category}}}}, 201);
    });
    server.Put(R"(/api/overdue/([A-Za-z0-9_-]+))", [&](const httplib::Request& req, httplib::Response& res) {
        cors(res); json body;
        try { body=json::parse(req.body); } catch (...) { sendJson(res,errorJson("Invalid JSON body"),400); return; }
        if(!body.is_object()||!body.contains("title")||!body["title"].is_string()||!body.contains("dueAt")||!body["dueAt"].is_string()){sendJson(res,errorJson("Wymagane pola title i dueAt."),400);return;}
        std::string title=body["title"].get<std::string>(), dueAt=body["dueAt"].get<std::string>(), notes=body.contains("notes")&&body["notes"].is_string()?body["notes"].get<std::string>():std::string();
        std::string priority=body.contains("priority")&&body["priority"].is_string()?body["priority"].get<std::string>():"normal", category=body.contains("category")&&body["category"].is_string()?body["category"].get<std::string>():"Ogólne";
        if((priority!="low"&&priority!="normal"&&priority!="high"&&priority!="urgent")||category.empty()||category.size()>80){sendJson(res,errorJson("Nieprawidłowy priorytet lub kategoria."),400);return;}
        if(title.empty()||title.size()>200||dueAt.empty()||dueAt.size()>40||notes.size()>2000){sendJson(res,errorJson("Sprawdź nazwę, termin i notatkę."),400);return;}
        std::lock_guard<std::mutex> lock(dbMutex); sqlite3_stmt* stmt=nullptr;
        if(sqlite3_prepare_v2(db,"UPDATE overdue_tasks SET title=?,due_at=?,notes=?,priority=?,category=?,updated_at=CURRENT_TIMESTAMP WHERE id=?",-1,&stmt,nullptr)!=SQLITE_OK){sendJson(res,errorJson("Database prepare failed"),500);return;}
        const std::string id=req.matches[1].str();sqlite3_bind_text(stmt,1,title.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(stmt,2,dueAt.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(stmt,3,notes.c_str(),static_cast<int>(notes.size()),SQLITE_TRANSIENT);sqlite3_bind_text(stmt,4,priority.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(stmt,5,category.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(stmt,6,id.c_str(),-1,SQLITE_TRANSIENT);
        int rc=sqlite3_step(stmt),changed=sqlite3_changes(db);sqlite3_finalize(stmt);if(rc!=SQLITE_DONE){sendJson(res,errorJson("Database update failed"),500);return;}if(!changed){sendJson(res,errorJson("Task not found"),404);return;}
        sendJson(res,{{"ok",true},{"task",{{"id",id},{"title",title},{"dueAt",dueAt},{"notes",notes},{"priority",priority},{"category",category}}}});
    });
    server.Post("/api/overdue/status", [&](const httplib::Request& req, httplib::Response& res) {
        cors(res); json body;
        try { body = json::parse(req.body); } catch (...) { sendJson(res, errorJson("Invalid JSON body"), 400); return; }
        if (!body.is_object() || !body.contains("id") || !body["id"].is_string() || !body.contains("completed") || !body["completed"].is_boolean()) { sendJson(res, errorJson("Fields id and completed are required"), 400); return; }
        std::string id = body["id"].get<std::string>(); bool completed = body["completed"].get<bool>();
        std::lock_guard<std::mutex> lock(dbMutex); sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, "UPDATE overdue_tasks SET completed=?,updated_at=CURRENT_TIMESTAMP WHERE id=?", -1, &stmt, nullptr) != SQLITE_OK) { sendJson(res, errorJson("Database prepare failed"), 500); return; }
        sqlite3_bind_int(stmt,1,completed?1:0); sqlite3_bind_text(stmt,2,id.c_str(),-1,SQLITE_TRANSIENT); int rc=sqlite3_step(stmt); int changed=sqlite3_changes(db); sqlite3_finalize(stmt);
        if (rc != SQLITE_DONE) { sendJson(res, errorJson("Database update failed"), 500); return; }
        if (!changed) { sendJson(res, errorJson("Task not found"), 404); return; }
        sendJson(res, {{"ok",true}});
    });
    server.Delete(R"(/api/overdue/([A-Za-z0-9_-]+))", [&](const httplib::Request& req, httplib::Response& res) {
        cors(res); std::lock_guard<std::mutex> lock(dbMutex); sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, "DELETE FROM overdue_tasks WHERE id=?", -1, &stmt, nullptr) != SQLITE_OK) { sendJson(res, errorJson("Database prepare failed"), 500); return; }
        std::string id=req.matches[1].str(); sqlite3_bind_text(stmt,1,id.c_str(),-1,SQLITE_TRANSIENT); int rc=sqlite3_step(stmt); sqlite3_finalize(stmt);
        if (rc != SQLITE_DONE) { sendJson(res, errorJson("Database delete failed"), 500); return; }
        sendJson(res, {{"ok",true}});
    });

#ifdef _WIN32
    windowsBlockerEnabled.store(loadWindowsBlockerSetting());
    std::thread windowsWorker(windowsBlockerWorker);
#endif
    std::cout << "Amogus core API listening on http://127.0.0.1:43127\n";
    std::cout << "Database: " << dataPath() << "\n";
    if (!server.listen("127.0.0.1", 43127)) {
        std::cerr << "Could not bind API to 127.0.0.1:43127\n";
        windowsWorkerStop.store(true);
#ifdef _WIN32
        if (windowsWorker.joinable()) windowsWorker.join();
#endif
        sqlite3_close(db); return 3;
    }
    windowsWorkerStop.store(true);
#ifdef _WIN32
    if (windowsWorker.joinable()) windowsWorker.join();
#endif
    sqlite3_close(db);
    return 0;
}
