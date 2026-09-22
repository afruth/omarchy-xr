#pragma once
#include <json-c/json.h>
#include <glib.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <memory>
#include <vector>
#include <set>
#include <string>

namespace notifications {
using Json=std::unique_ptr<json_object,decltype(&json_object_put)>;
using Color=std::array<double,4>;
struct Card {
    std::string generation,key,app,summary,body;
    Color background{.08,.1,.15,1},text{.9,.92,.98,1},accent{.4,.7,1,1};
    bool operator==(const Card&) const=default;
    std::string identity() const {return std::to_string(generation.size())+":"+generation+key;}
};
inline json_object* field(json_object* object,const char* name) {
    json_object* value=nullptr;json_object_object_get_ex(object,name,&value);return value;
}
inline std::string string(json_object* object,const char* name,size_t limit) {
    auto* value=field(object,name);
    if(!value || json_object_get_type(value)!=json_type_string) return {};
    const std::string bounded(json_object_get_string(value),std::min(size_t(json_object_get_string_len(value)),limit));
    char* valid=g_utf8_make_valid(bounded.data(),bounded.size());std::string result(valid);g_free(valid);return result;
}
inline Color color(const std::string& text,Color fallback) {
    if((text.size()!=7 && text.size()!=9) || text[0]!='#' || text.find_first_not_of("0123456789abcdefABCDEF",1)!=std::string::npos) return fallback;
    const size_t start=text.size()==9?3:1;
    for(int i=0;i<3;++i) fallback[i]=std::stoul(text.substr(start+i*2,2),nullptr,16)/255.;
    fallback[3]=text.size()==9?std::stoul(text.substr(1,2),nullptr,16)/255.:1;
    return fallback;
}
inline Json parse(const std::string& data) {
    if(data.empty() || data.size()>1024*1024) return {nullptr,json_object_put};
    auto* tokener=json_tokener_new();
    json_tokener_set_flags(tokener,JSON_TOKENER_STRICT);
    auto* object=json_tokener_parse_ex(tokener,data.c_str(),int(data.size()+1));
    const bool valid=json_tokener_get_error(tokener)==json_tokener_success;
    json_tokener_free(tokener);
    if(!valid && object) {json_object_put(object);object=nullptr;}
    return {object,json_object_put};
}
inline std::vector<Card> readCards(const std::string& data,double wallMs) {
    auto root=parse(data);if(!root || json_object_get_type(root.get())!=json_type_object) return {};
    const double stamp=json_object_get_double(field(root.get(),"time"));
    auto* entries=field(root.get(),"entries");
    if(json_object_get_int(field(root.get(),"version"))!=1 || !std::isfinite(stamp) || wallMs<stamp || wallMs-stamp>3000
        || !entries || json_object_get_type(entries)!=json_type_array || !json_object_array_length(entries)) return {};
    std::vector<Card> cards;
    std::set<std::string> seen;
    for(size_t i=0;i<std::min<size_t>(32,json_object_array_length(entries));++i) {
        auto* row=json_object_array_get_idx(entries,i);
        if(!row || json_object_get_type(row)!=json_type_object)continue;
        Card card;card.generation=string(root.get(),"generation",128);card.key=string(row,"key",128);
        if(card.generation.empty() || card.key.empty() || !seen.insert(card.identity()).second)continue;
        card.app=string(row,"app",256);card.summary=string(row,"summary",8192);card.body=string(row,"body",32768);
        auto* palette=field(root.get(),"palette");
        card.background=color(string(palette,"background",9),card.background);
        card.text=color(string(palette,"text",9),card.text);
        card.accent=color(string(palette,json_object_get_int(field(row,"urgency"))==2?"urgent":"accent",9),card.accent);
        cards.push_back(std::move(card));
    }
    return cards;
}
inline std::string readFile(const std::string& path) {
    std::ifstream file(path,std::ios::binary);std::string data(1024*1024+1,'\0');
    file.read(data.data(),data.size());data.resize(size_t(file.gcount()));return data;
}
inline std::string dismissal(const Card& card,double wallMs) {
    Json object(json_object_new_object(),json_object_put);
    json_object_object_add(object.get(),"generation",json_object_new_string(card.generation.c_str()));
    json_object_object_add(object.get(),"key",json_object_new_string(card.key.c_str()));
    json_object_object_add(object.get(),"time",json_object_new_double(wallMs));
    return json_object_to_json_string_ext(object.get(),JSON_C_TO_STRING_PLAIN);
}
// Body markup is converted to plain text. It never enters a rich-text renderer,
// loads images/URLs, or executes actions; those remain on the native desktop card.
inline std::string plainBody(const std::string& source) {
    std::string text;
    for(size_t i=0;i<source.size();) {
        if(source[i]=='<') {
            const auto end=source.find('>',i);if(end==std::string::npos) {text+=source.substr(i);break;}
            auto tag=source.substr(i+1,end-i-1);
            std::transform(tag.begin(),tag.end(),tag.begin(),[](unsigned char c){return char(g_ascii_tolower(c));});
            if(tag=="br" || tag=="br/" || tag=="br /" || tag=="/p" || tag=="/div") text+='\n';
            i=end+1;continue;
        }
        if(source[i]=='&') {
            const auto end=source.find(';',i);
            if(end!=std::string::npos && end-i<16) {
                const auto entity=source.substr(i+1,end-i-1);
                const std::pair<const char*,const char*> named[]={{"amp","&"},{"lt","<"},{"gt",">"},{"quot","\""},{"apos","'"},{"nbsp"," "}};
                bool found=false;
                for(auto [name,value]:named) if(entity==name) {text+=value;found=true;break;}
                if(!entity.empty() && entity[0]=='#') {
                    char* tail=nullptr;const bool hex=entity.size()>1 && (entity[1]=='x' || entity[1]=='X');
                    const auto code=std::strtoul(entity.c_str()+(hex?2:1),&tail,hex?16:10);
                    if(tail && !*tail && code>0 && code<=0x10ffff && g_unichar_validate(gunichar(code))) {
                        char utf8[6];text.append(utf8,size_t(g_unichar_to_utf8(gunichar(code),utf8)));found=true;
                    }
                }
                if(found) {i=end+1;continue;}
            }
        }
        text+=source[i++];
    }
    return text;
}
}
