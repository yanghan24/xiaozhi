#include "../main/identity_loss_main.cc"
#include <cstring>
#include <iostream>

struct TestIterator {std::vector<Key> keys;size_t position=0;};
static Snapshot database;
static std::map<nvs_handle_t,std::string> handles;
static std::string response;
static int erases=0,commits=0;
static bool read_failure=false,write_open_failure=false,commit_failure=false,mutate_other=false;
static const std::string identity_json =
    "{\"version\":1,\"hardware_id\":\"112233445566\",\"device_id\":\"test-b\","
    "\"credential\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\","
    "\"ota_url\":\"https://example.com/xiaozhi/ota/\",\"control_origin\":\"\"}";

static std::vector<uint8_t> Bytes(const std::string& value,bool zero=false) {
    auto result=std::vector<uint8_t>(value.begin(),value.end());
    if(zero) result.push_back(0);
    return result;
}
static void Fresh() {
    database.clear();handles.clear();response.clear();write_attempted=false;erases=commits=0;
    read_failure=write_open_failure=commit_failure=mutate_other=false;
    database[{"device_ctrl","identity_v1"}]={NVS_TYPE_BLOB,Bytes(identity_json)};
    database[{"device_ctrl","device_id"}]={NVS_TYPE_STR,Bytes("test-b",true)};
    database[{"wifi","ssid"}]={NVS_TYPE_STR,Bytes("configured-network",true)};
    database[{"other","empty"}]={NVS_TYPE_BLOB,{}};
    database[{"other","blob"}]={NVS_TYPE_BLOB,{0,1,0,255}};
    int index=0;
    for(auto type:{NVS_TYPE_I8,NVS_TYPE_U8,NVS_TYPE_I16,NVS_TYPE_U16,NVS_TYPE_I32,NVS_TYPE_U32,NVS_TYPE_I64,NVS_TYPE_U64}) {
        const size_t size=1u<<(index/2);
        database[{"other","number"+std::to_string(index++)}]={type,std::vector<uint8_t>(size,0xa5)};
    }
}
static std::string Frame(const std::string& identity=identity_json,const std::string& confirm=kConfirmation) {
    const auto root=std::unique_ptr<cJSON,decltype(&cJSON_Delete)>(cJSON_CreateObject(),cJSON_Delete);
    cJSON_AddNumberToObject(root.get(),"version",1);
    cJSON_AddStringToObject(root.get(),"operation_id","0123456789abcdef0123456789abcdef");
    cJSON_AddStringToObject(root.get(),"action","erase_identity_v1");
    cJSON_AddItemToObject(root.get(),"identity",cJSON_Parse(identity.c_str()));
    cJSON_AddStringToObject(root.get(),"confirmation",confirm.c_str());
    std::unique_ptr<char,decltype(&cJSON_free)> text(cJSON_PrintUnformatted(root.get()),cJSON_free);
    return text.get();
}
static std::string Result() {
    std::unique_ptr<cJSON,decltype(&cJSON_Delete)> root(cJSON_Parse(response.c_str()),cJSON_Delete);
    assert(root);
    const auto* value=cJSON_GetObjectItemCaseSensitive(root.get(),"result");
    assert(cJSON_IsString(value));
    return value->valuestring;
}
static void Send(const std::string& text) {response.clear();Apply(text);}
static std::string Replace(std::string text,const std::string& from,const std::string& to) {
    const auto position=text.find(from);assert(position!=std::string::npos);
    text.replace(position,from.size(),to);return text;
}

esp_err_t nvs_open(const char* ns,nvs_open_mode_t mode,nvs_handle_t* handle) {
    if(mode==NVS_READWRITE && write_open_failure) return ESP_FAIL;
    const auto id=static_cast<nvs_handle_t>(handles.size()+1);handles[id]=ns;*handle=id;return ESP_OK;
}
void nvs_close(nvs_handle_t) {}
static esp_err_t Read(nvs_handle_t handle,const char* key,nvs_type_t type,void* data,size_t* size) {
    if(read_failure && std::string(key)=="ssid") return ESP_FAIL;
    const auto found=database.find({handles.at(handle),key});
    if(found==database.end()) return ESP_ERR_NVS_NOT_FOUND;
    if(found->second.type!=type) return ESP_ERR_NVS_TYPE_MISMATCH;
    const auto& bytes=found->second.bytes;
    if(!data) {*size=bytes.size();return ESP_OK;}
    if(*size<bytes.size()) return ESP_ERR_INVALID_SIZE;
    std::memcpy(data,bytes.data(),bytes.size());*size=bytes.size();return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t h,const char* k,void* d,size_t* s) {return Read(h,k,NVS_TYPE_BLOB,d,s);}
esp_err_t nvs_get_str(nvs_handle_t h,const char* k,char* d,size_t* s) {return Read(h,k,NVS_TYPE_STR,d,s);}
esp_err_t nvs_set_blob(nvs_handle_t,const char*,const void*,size_t) {assert(false);return ESP_FAIL;}
esp_err_t nvs_set_str(nvs_handle_t,const char*,const char*) {assert(false);return ESP_FAIL;}
esp_err_t nvs_erase_all(nvs_handle_t) {assert(false);return ESP_FAIL;}
#define NUMBER_GETTER(suffix,type,kind) \
esp_err_t nvs_get_##suffix(nvs_handle_t h,const char* k,type* d) {size_t s=sizeof(type);return Read(h,k,kind,d,&s);}
NUMBER_GETTER(i8,int8_t,NVS_TYPE_I8) NUMBER_GETTER(u8,uint8_t,NVS_TYPE_U8)
NUMBER_GETTER(i16,int16_t,NVS_TYPE_I16) NUMBER_GETTER(u16,uint16_t,NVS_TYPE_U16)
NUMBER_GETTER(i32,int32_t,NVS_TYPE_I32) NUMBER_GETTER(u32,uint32_t,NVS_TYPE_U32)
NUMBER_GETTER(i64,int64_t,NVS_TYPE_I64) NUMBER_GETTER(u64,uint64_t,NVS_TYPE_U64)
#undef NUMBER_GETTER
esp_err_t nvs_erase_key(nvs_handle_t handle,const char* key) {
    assert(handles.at(handle)=="device_ctrl" && std::string(key)=="identity_v1");
    ++erases;return database.erase({handles.at(handle),key})==1?ESP_OK:ESP_ERR_NVS_NOT_FOUND;
}
esp_err_t nvs_commit(nvs_handle_t handle) {
    assert(handles.at(handle)=="device_ctrl");++commits;
    if(mutate_other) database.at({"wifi","ssid"}).bytes[0]^=1;
    return commit_failure?ESP_FAIL:ESP_OK;
}
esp_err_t nvs_entry_find(const char* partition,const char* ns,nvs_type_t type,nvs_iterator_t* out) {
    assert(std::string(partition)=="nvs" && !ns && type==NVS_TYPE_ANY);
    auto iterator=new TestIterator;
    for(const auto& item:database) iterator->keys.push_back(item.first);
    if(iterator->keys.empty()) {delete iterator;*out=nullptr;return ESP_ERR_NVS_NOT_FOUND;}
    *out=iterator;return ESP_OK;
}
esp_err_t nvs_entry_next(nvs_iterator_t* iterator) {
    return ++(*iterator)->position<(*iterator)->keys.size()?ESP_OK:ESP_ERR_NVS_NOT_FOUND;
}
esp_err_t nvs_entry_info(nvs_iterator_t iterator,nvs_entry_info_t* info) {
    const auto& key=iterator->keys.at(iterator->position);
    std::snprintf(info->namespace_name,sizeof(info->namespace_name),"%s",key.first.c_str());
    std::snprintf(info->key,sizeof(info->key),"%s",key.second.c_str());
    info->type=database.at(key).type;return ESP_OK;
}
void nvs_release_iterator(nvs_iterator_t iterator) {delete iterator;}
esp_err_t nvs_flash_init() {return ESP_OK;}
esp_err_t esp_efuse_mac_get_default(uint8_t* mac) {
    const uint8_t fixed[]={0x11,0x22,0x33,0x44,0x55,0x66};std::memcpy(mac,fixed,6);return ESP_OK;
}
int uart_write_bytes(uart_port_t,const void* data,size_t size) {response.append(static_cast<const char*>(data),size);return size;}
esp_err_t uart_wait_tx_done(uart_port_t,int) {return ESP_OK;}
esp_err_t uart_param_config(uart_port_t,const uart_config_t*) {return ESP_OK;}
bool uart_is_driver_installed(uart_port_t) {return true;}
esp_err_t uart_driver_install(uart_port_t,int,int,int,void*,int) {return ESP_OK;}
int uart_read_bytes(uart_port_t,void*,uint32_t,int) {return 0;}
void esp_log_level_set(const char*,int) {}

int main() {
    int scenarios=0;
    const auto reject=[&](const std::string& frame,const std::string& expected) {
        Fresh();const auto initial=database;Send(frame);assert(Result()==expected);
        assert(erases==0 && commits==0 && database==initial && !write_attempted);++scenarios;
    };
    reject(Frame(identity_json,"not-confirmed"),"invalid_request");
    reject(Replace(Frame(),"\"version\":1","\"version\":2"),"invalid_request");
    reject(Frame().substr(0,Frame().size()-1)+",\"version\":1}","invalid_request");
    reject(Frame().substr(0,Frame().size()-1)+",\"arbitrary_key\":\"wifi\"}","invalid_request");
    reject(Replace(Frame(),"0123456789abcdef0123456789abcdef","bad-operation"),"invalid_request");
    reject(Frame(Replace(identity_json,"112233445566","112233445567")),"invalid_identity");
    reject(Frame(Replace(identity_json,"test-b","another-device")),"identity_mismatch");
    reject(Frame(Replace(identity_json,std::string(43,'a'),std::string(43,'b'))),"identity_mismatch");
    reject(Frame()+std::string(1,'\0'),"invalid_request");
    reject(Replace(Frame(),"erase_identity_v1","erase_all"),"invalid_request");

    Fresh();const auto before=database;Send(Frame());assert(Result()=="identity_erased");
    auto wanted=before;wanted.erase({"device_ctrl","identity_v1"});
    assert(database==wanted && erases==1 && commits==1 && write_attempted);
    device_control::IdentityConfig absent;assert(!device_control::LoadDeviceIdentity(absent));
    Send(Frame());assert(Result()=="write_already_attempted" && erases==1 && commits==1);++scenarios;

    Fresh();commit_failure=true;Send(Frame());assert(Result()=="storage_unverified");
    Send(Frame());assert(Result()=="write_already_attempted" && erases==1 && commits==1);++scenarios;

    Fresh();mutate_other=true;Send(Frame());assert(Result()=="postcondition_unverified");
    Send(Frame());assert(Result()=="write_already_attempted" && erases==1);++scenarios;

    Fresh();read_failure=true;const auto original=database;Send(Frame());
    assert(Result()=="snapshot_unavailable" && erases==0 && commits==0 && database==original);++scenarios;

    Fresh();write_open_failure=true;Send(Frame());assert(Result()=="storage_unverified");
    Send(Frame());assert(Result()=="write_already_attempted" && erases==0 && commits==0);++scenarios;

    Fresh();database.erase({"device_ctrl","identity_v1"});Send(Frame());
    assert(Result()=="identity_mismatch" && erases==0);++scenarios;

    Fresh();database.at({"device_ctrl","device_id"}).type=NVS_TYPE_BLOB;Send(Frame());
    assert(Result()=="identity_mismatch" && erases==0);++scenarios;
    std::cout<<scenarios<<" guard scenarios passed\n";
}
