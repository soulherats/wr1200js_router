#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <openssl/evp.h>
#include <memory>
#include "tinyjson.hpp"
#include <curl/curl.h>
#include <fstream>

using namespace tiny;

// 内联函数
inline bool isHex(char c) {
    return std::isxdigit(static_cast<unsigned char>(c));
}

// 全局函数
const std::vector<std::string> split(const std::string &str, const char &delimiter) {
    std::vector<std::string> result;
    std::stringstream ss(str);
    std::string tok;

    while (std::getline(ss, tok, delimiter)) {
        result.push_back(tok);
    }
    return result;
}

bool hasUrlEncoded(const std::string& s) {
    for (size_t i = 0; i + 2 < s.size(); ++i) {
        if (s[i] == '%' && isHex(s[i + 1]) && isHex(s[i + 2])) {
            return true;
        }
    }
    return false;
}

class Protocol {
public:
	std::string protocol;
	std::string name;   // 名字
	std::string server_name;  // 服务器
	unsigned int port;   // 端口
	std::string password;  // 密码
	std::string obfs;	// 混淆
	// 成员函数
	static bool base64Decode(const std::string& encoded, std::string& decoded) {
		size_t src_len = encoded.size(), decode_len;
		int ret, i = 0;

		if (src_len % 4 != 0) return false;
		decode_len = (src_len / 4) * 3;
		decoded.resize(decode_len);

		ret = EVP_DecodeBlock((unsigned char*)decoded.c_str(), (const unsigned char*)encoded.c_str(), (int)src_len);
		if (ret == -1) {
			decoded = encoded;
			return false;
		}

		while (encoded.at(--src_len) == '=') {
			ret--;
			if (++i > 2) return false;
		}
		decoded.resize(ret);
		return true;
	}

	virtual TinyJson json_output() {
		TinyJson example;
		return example;
	}

	static std::string urlDecode(std::string &eString) {
		std::string ret;
		char ch;
		unsigned int i, j;
		for (i=0; i<eString.length(); i++) {
			if (int(eString[i])==37) {
				sscanf(eString.substr(i+1,2).c_str(), "%x", &j);
				ch=static_cast<char>(j);
				ret+=ch;
				i=i+2;
			} else {
				ret+=eString[i];
			}
		}
		return (ret);
	}

	static std::string Base64Encode(const unsigned char* data, size_t size)
	{
		size_t base64_len = (size + 2) / 3 * 4;
		if (base64_len == 0)
		{
			return "";
		}
		std::string ret;
		ret.resize(base64_len);
		EVP_EncodeBlock((unsigned char*)ret.data(), data, size);
		return std::move(ret);
	}

	static bool isBase64Char(char c) {
		return (std::isalnum(c) || c == '+' || c == '/' || c == '=');
	}

	static size_t findBase64End(const std::string& input) {
		size_t pos = 0;
		while (pos < input.size() && isBase64Char(input[pos])) {
			++pos;
		}
		return pos;
	}
};

class Shadowsocks: public Protocol {
private:
	static int count;

	void parseData(const std::string& data) {
		std::vector<std::string> param, child;
		std::string b64_str, str, b64_dstr;
		size_t pos;

		if (data.empty()) {
			return;
		}

		// name
		pos = data.find('#');
		str = data.substr(0, pos);
		if (pos != std::string::npos) {
			name = data.substr(pos + 1);
		}

		// obfs
		pos = str.find('?'); 
		if (pos != std::string::npos) {
			obfs = str.substr(pos + 1);
			obfs = urlDecode(obfs);
			//std::cout << obfs << std::endl;
		}
		str = str.substr(0, pos);
		
		//解析ss://method:password@server:port
		// 解析base64
		pos = findBase64End(str);
		b64_str = data.substr(0, pos);
		while (b64_str.length() % 4 != 0) b64_str += '=';
		base64Decode(b64_str, b64_dstr);
		str.replace(0, pos, b64_dstr);
	
		child = split(str, '@');
		for (std::string& it : child) {
			pos = it.find(':');
			if (pos == std::string::npos) {
				base64Decode(it, it);
				pos = it.find(':');
			}
			param.push_back(it.substr(0, pos));
			param.push_back((pos != std::string::npos) ? it.substr(pos + 1) : "");
		}	

		//初始化赋值
		protocol = "ss";
		method = param[0];
		password = param[1];
		server_name = param[2];
		port = std::stoi(param[3]); 
		return;
	}
public:
	std::string method;

	Shadowsocks(const std::string& ss) {
		++count;
		parseData(ss);
	}

	Shadowsocks(const std::string& a, const std::string& b, unsigned int c, \
			const std::string& d, const std::string& e, const std::string& f) {
		++count;
		name = a;
		server_name = b;
		port = c;
		password = d;
		protocol = "ss";
		method = e;
		obfs = f;
	}

	TinyJson json_output() override {
		TinyJson ss_json;
		std::string ss_name = name;
		ss_json["proto"].Set(protocol);
		ss_json["server"].Set(server_name);
		ss_json["port"].Set(port);
		ss_json["method"].Set(method);
		ss_json["password"].Set(password);
		ss_json["name"].Set(Base64Encode((const unsigned char*)name.c_str(), (int)name.size()));

		if (hasUrlEncoded(ss_name)) ss_name = urlDecode(name); 
		std::cout << ss_name << " " << protocol <<  " " << server_name << " " << port << " " << method << " " << password << " " << obfs << std::endl; // 输出解码后的字符串
		return ss_json; 
	}

	static int getCount() {
		return count;
	}
};

// 初始化静态成员变量
int Shadowsocks::count = 0;

std::unique_ptr<Protocol> createProtocol(const std::string& type, const std::string& ss_proto) {
	if (type == "ss") {
		return std::make_unique<Shadowsocks>(ss_proto);
	} else {
		return nullptr;
	}
}

size_t req_reply(void *ptr, size_t size, size_t nmemb, void *stream)
{
    std::string *str = (std::string*)stream;
    (*str).append((char*)ptr, size*nmemb);
    return size * nmemb;
}

CURLcode curl_get_req(const std::string &url, std::string &response)
{
    // init curl
    CURL *curl = curl_easy_init();
    // res code
    CURLcode res = CURLE_OK;
    if (curl)
    {
        // set params
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str()); // url
       	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L); // if want to use https
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L); // set peer and host verify false
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/143.0.0.0 Safari/537.36 Edg/143.0.0.0");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, req_reply);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&response);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        // start req
        res = curl_easy_perform(curl);
    }
    // release curl
    curl_easy_cleanup(curl);
    return res;
}

int main(int argc, char *argv[]) {
	std::string  decoded, encoded, input;
	std::string str, ss_file = "/tmp/ss_link"; // 输出字符串
	std::vector<std::string> tokens; // 逐行输入
	std::vector<std::unique_ptr<Protocol>> protos;
	TinyJson jsonarray;
	std::fstream f;
	
	if (argc < 2) {
		std::cout << "输入url" << std::endl;
		return 1;
	}

	input = argv[1];
	curl_global_init(CURL_GLOBAL_ALL);
	if (input.find("http://") == 0 || input.find("https://") == 0) {
		if (curl_get_req(input, encoded) != CURLE_OK) {
			return 2;
		}
	} else {
		std::cerr << "无效的 URL: " << input << "\n";
		return 1;
	}

	if (Protocol::base64Decode(encoded, decoded)) {
		tokens = split(decoded, '\n');
		for (auto it = tokens.begin(); it != tokens.end(); ++it) {
			size_t pos = (*it).find_first_of("://"); //协议分割符
			if (pos == std::string::npos)  continue;
			auto proto = createProtocol((*it).substr(0, pos), (*it).substr(pos + 3));
			if (proto == nullptr) continue;
			protos.push_back(std::move(proto));
		}
		for (const auto& test : protos) {
			jsonarray.Push(test->json_output());
		}
		std::string str = jsonarray.WriteJson(2);
		f.open(ss_file,std::ios::out);
		f << str << std::endl;
    }
    return 0;
}

