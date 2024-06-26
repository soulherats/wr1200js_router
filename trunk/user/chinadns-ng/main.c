#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <curl/curl.h>

#define BASE64_SUCCESS 0
#define BASE64_ERROR 1
#define BASE64_ERR_BASE64_BAD_MSG 2

// 回调函数处理HTTP响应
size_t write_callback(void *ptr, size_t size, size_t nmemb, void **stream) {
    size_t realsize = size * nmemb;
    if(NULL != *stream)
    {
	size_t len = strlen(*(char **)stream);
    	*stream = (char*)realloc(*stream, realsize + len + 1);
	memcpy(*stream + len, (char*)ptr, realsize);
	(*(char **)stream)[len + realsize] = '\0';
    } else {
	*stream = (char*)malloc(realsize + 1);
	memcpy(*stream,(char*)ptr, realsize);
	(*(char **)stream)[realsize] = '\0';
    }

    return realsize;
}

int http_get_upgrade_file(char** response, const char* url)
{
    CURL *curl;
    CURLcode __attribute__ ((unused)) res;
    char *final_url = NULL;
    curl = curl_easy_init();
    long httpcode = -1;
    if(curl)
    {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        if(strstr(url,"https"))
        {
            curl_easy_setopt(curl, CURLOPT_DEFAULT_PROTOCOL, "https");
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        }
        else
        {
            curl_easy_setopt(curl, CURLOPT_DEFAULT_PROTOCOL, "http");
        }
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 60L);
        res = curl_easy_perform(curl);
	if(res != CURLE_OK)
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        curl_easy_getinfo(curl, CURLINFO_HTTP_CODE, &httpcode);
	curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &final_url);
        curl_easy_cleanup(curl);
    }

    if (httpcode != 200)
    {
	printf("HttpCode error:%ld try to check ip\n", httpcode);
        return -1;
    }
    return 0;
}

static uint8_t get_index_from_char(char c)
{
    if ((c >= 'A') && (c <= 'Z'))           return (c - 'A');
    else if ((c >= 'a') && (c <= 'z'))      return (c - 'a' + 26);
    else if ((c >= '0') && (c <= '9'))      return (c - '0' + 52);
    else if (c == '+' || c == '-')                      return 62;
    else if (c == '/' || c == '_')                      return 63;
    else if (c == '=')                      return 64;
    else if ((c == 'r') || (c == 'n'))    return 254;
    else                                    return 255;
}

static char get_char_from_index(uint8_t i)
{
    if ((i >= 0) && (i <= 25))              return (i + 'A');
    else if ((i >= 26) && (i <= 51))        return (i - 26 + 'a');
    else if ((i >= 52) && (i <= 61))        return (i - 52 + '0');
    else if (i == 62)                       return '+';
    else if (i == 63)                       return '/';
    else                                    return '=';
}

int base64_encode(const uint8_t *in, uint16_t in_len, char *out)
{
    int i;
    uint32_t tmp = 0;
    uint16_t out_len = 0;
    uint16_t left = in_len;

    if ((!in) || (!out)) {
        //invalid parameter
        return BASE64_ERROR;
    }

    for (i = 0; i < in_len;) {
        if (left >= 3) {
            tmp = in[i];
            tmp = (tmp << 8) + in[i+1];
            tmp = (tmp << 8) + in[i+2];
            out[out_len++] = get_char_from_index((tmp & 0x00FC0000) >> 18);
            out[out_len++] = get_char_from_index((tmp & 0x0003F000) >> 12);
            out[out_len++] = get_char_from_index((tmp & 0x00000FC0) >> 6);
            out[out_len++] = get_char_from_index(tmp & 0x0000003F);
            left -= 3;
            i += 3;
        } else {
            break;
        }
    }

    if (left == 2) {
        tmp = in[i];
        tmp = (tmp << 8) + in[i+1];
        out[out_len++] = get_char_from_index((tmp & 0x0000FC00) >> 10);
        out[out_len++] = get_char_from_index((tmp & 0x000003F0) >> 4);
        out[out_len++] = get_char_from_index((tmp & 0x0000000F) << 2);
        out[out_len++] = get_char_from_index(64);
    } else if (left == 1) {
        tmp = in[i];
        out[out_len++] = get_char_from_index((tmp & 0x000000FC) >> 2);
        out[out_len++] = get_char_from_index((tmp & 0x00000003) << 4);
        out[out_len++] = get_char_from_index(64);
        out[out_len++] = get_char_from_index(64);
    }

    out[out_len] = '\0';

    return BASE64_SUCCESS;
}

int base64_decode(const char *in, uint8_t *out, uint16_t *out_len)
{
    uint16_t i = 0, cnt = 0;
    uint8_t c, in_data_cnt;
    int error_msg = 1;
    uint32_t tmp = 0;

    if ((!in) || (!out) || (!out_len)) {
        //invalid parameter
        return BASE64_ERROR;
    }

    in_data_cnt = 0;
    while (in[i] != '\0') {
        c = get_index_from_char(in[i++]);
        if (c == 255) {
            return BASE64_ERR_BASE64_BAD_MSG;
        } else if (c == 254) {
            continue;           // Carriage return or newline feed, skip
        } else if (c == 64) {
            break;              // Meet '=', break
        }

        tmp = (tmp << 6) | c;
        if (++in_data_cnt == 4) {
            out[cnt++] = (uint8_t)((tmp >> 16) & 0xFF);
            out[cnt++] = (uint8_t)((tmp >> 8) & 0xFF);
            out[cnt++] = (uint8_t)(tmp & 0xFF);
            in_data_cnt = 0;
            tmp = 0;
        }
    }

    // Meet '=' or ''
    if (in_data_cnt == 3) {          // 3 chars before '=', encoded msg like xxx= OR
        tmp = (tmp << 6);           // 3 chars before '', encoded msg like xxx, considered '=' omitted
        out[cnt++] = (uint8_t)((tmp >> 16) & 0xFF);
        out[cnt++] = (uint8_t)((tmp >> 8) & 0xFF);
    } else if (in_data_cnt == 2) {   // 2 chars before '=', encoded msg like xx== OR
        tmp = (tmp << 6);           // 2 chars before '', encoded msg like xx, considered '=' omitted
        tmp = (tmp << 6);
        out[cnt++] = (uint8_t)((tmp >> 16) & 0xFF);
    } else if (in_data_cnt != 0) {
        error_msg = 0;           // Warn that the message format is wrong, but we tried our best to decode
    }

    *out_len = cnt;

    return (error_msg ? BASE64_ERROR : BASE64_SUCCESS);
}

size_t print_ss(char *str, char *name, char *proto, char* decode_data) {
	uint16_t j = 0, len, total = 0; //计数器
	char tmp[50] = {0};
	char seps[] = ":@/?&", *token = NULL, *context = NULL, *data = decode_data;

	data += sprintf(data,"[\"%s\",", proto);
	for(token = strtok_r(str, seps, &context); token != NULL; token = strtok_r( NULL, seps, &context), j++) {
		if (j >= 5) {
			char *ptr = NULL, sep = '=' , *ret;
			int flag = 0;
			if (token[strlen(token) - 1] == '=') flag = 1;
			for(ptr = strtok(token, &sep); ptr != NULL; ptr = strtok( NULL, &sep)) {
				if (j % 2) {
					base64_decode(ptr, tmp, &len);
					tmp[len] = '\0';
					//if (j == 5) data += sprintf(data, "password:");
					data += sprintf(data,"\"%s\"", tmp);
					if (j != 13) data += sprintf(data,",");
				} else {
					j++;
					//data += sprintf(data,"\"%s\":", ptr);
				}
			}
			//if (flag) data += sprintf(data,"\"\"},");
			if (flag) data += sprintf(data,"\"\",");
		} else
			data += sprintf(data,"\"%s\",", token);
	}
	if (!proto[2]) data += sprintf(data,"\"%s\"", name);
	data += sprintf(data,"]");
	return data - decode_data;
}

size_t ParseProtocol(char *str, char* out_buff) {
	char proto[6] = {0}, data[400] = {0}, name[20] = {0};
	uint16_t len = 0;
	sscanf(str, "%[^:]://%[^#]#%s", proto, data, name);
	if (data[0] == '\0') return 1; //丢弃非链接

	base64_decode(data, str, &len);
	str[len] = '\0';

	return print_ss(str, name, proto, out_buff);
}

void ParseData(char* data) {
	char line[512], *ptr = data, *ptw = data; /*ptw for write*/
	while (sscanf(ptr, "%s", line) == 1) {
		for (ptr += strlen(line); *ptr && *ptr != '\n' && *ptr != '\r'; ptr++) {};
		if (ptw == data) sprintf(ptw++,"[");
		ptw += ParseProtocol(line, ptw);
		if (*(ptr+1)) sprintf(ptw++,",");
	}
	sprintf(ptw,"]\n");
}


int main(int argc, char *argv[])
{
	FILE *fp = NULL;
	char *data = NULL;
	char *decoded_data = NULL;
	uint16_t decoded_length = 0;

	if (argc < 2) {
		printf("Usage: %s [URL]\n", argv[0]);
		return 1;
	}

	if (!http_get_upgrade_file(&data, argv[1])) {
		decoded_data = (char*)malloc(strlen(data));
		base64_decode(data, decoded_data, &decoded_length);
		decoded_data[decoded_length] = '\0';
		ParseData(decoded_data);
	}

	fp = fopen("/tmp/ss_link", "wb");
	if (fp) {
		fprintf(fp, "%s", decoded_data);
		fclose(fp);
	}

	if (data) free(data);  // 释放动态分配的内存
	if (decoded_data) free(decoded_data);
	return 0;
}
