#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <curl/curl.h>

#define PAD_MEM 100

const char base64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// 自定义数据结构，用于存储下载的数据
struct MemoryStruct {
    char *memory;
    size_t size;
};

void base64_decode(const char *input, unsigned char *output, size_t *output_length) {
    int len = strlen(input);
    int pad = 0;
    if (input[len - 1] == '=') pad++;
    if (input[len - 2] == '=') pad++;

    *output_length = 3 * (len / 4) - pad;
    unsigned char *p = output;

    for (int i = 0; i < len; i += 4) {
        int n = (strchr(base64_table, input[i]) - base64_table) << 18 |
                (strchr(base64_table, input[i + 1]) - base64_table) << 12 |
                (strchr(base64_table, input[i + 2]) ? (strchr(base64_table, input[i + 2]) - base64_table) << 6 : 0) |
                (strchr(base64_table, input[i + 3]) ? (strchr(base64_table, input[i + 3]) - base64_table) : 0);

        *p++ = (n >> 16) & 0xFF;
        if (input[i + 2] != '=') *p++ = (n >> 8) & 0xFF;
        if (input[i + 3] != '=') *p++ = n & 0xFF;
    }
}

// 回调函数用于处理返回的数据
size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t total_size = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;

    // 重新分配内存以容纳新数据
    char *ptr = realloc(mem->memory, mem->size + total_size + 1);
    if(ptr == NULL) {
        // 内存分配失败
        fprintf(stderr, "Not enough memory (realloc returned NULL)\n");
        return 0;
    }

    // 更新指针和大小
    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, total_size);
    mem->size += total_size;
    mem->memory[mem->size] = 0; // 终止字符串

    return total_size;
}

size_t calculate_length(size_t len) {
    return len + (4 - (len % 4)) % 4; // 计算需要的填充符数量
}

int is_base64(char *str) {
    size_t len = strlen(str);

    // Base64字符串长度必须为4的倍数
    if (len % 4 != 0) {
        return 1;
    }

    // 检查有效字符和填充条件并替换-_
    for (size_t i = 0; i < len; ++i) {
        if (!isalnum(str[i]) && str[i] != '+' && str[i] != '/' && str[i] != '=' && str[i] != '-' && str[i] != '_') {
            return 1;
        } else if (str[i] == '-') {
	    str[i] = '+';
	} else if (str[i] == '_') {
	    str[i] = '/';
	}
    }

    // 检查填充符号是否只在末尾，并且不能超过两个
    int padding_count = 0;
    for (int i = len - 1; i >= 0 && str[i] == '='; --i) {
        padding_count++;
    }
    if (padding_count > 2) {
        return 1;
    }

    // 'P' should not be between characters
    for (size_t i = 0; i < len - padding_count; ++i) {
        if (str[i] == '=') {
            return 1;
        }
    }

    return 0;
}

int hex_to_int(char c) {
    if ('0' <= c && c <= '9') return c - '0';
    if ('a' <= c && c <= 'f') return c - 'a' + 10;
    if ('A' <= c && c <= 'F') return c - 'A' + 10;
    return -1; // 非法字符
}

void url_decode(const char *src, char *dest) {
    char *p = dest;
    while (*src) {
        if (*src == '%') {
            if (isxdigit((unsigned char)src[1]) && isxdigit((unsigned char)src[2])) {
                // 解码%xx
                int high = hex_to_int(src[1]);
                int low = hex_to_int(src[2]);
                *p++ = (char)((high << 4) | low);
                src += 3;
            } else {
                // 如果%后不是合法的两位16进制，直接跳过
                *p++ = *src++;
            }
        } else if (*src == '+') {
            // '+' 转换为空格
            *p++ = ' ';
            src++;
        } else {
            // 其他字符原样复制
            *p++ = *src++;
        }
    }
    *p = '\0'; // 确保字符串以NULL结尾
}

char* url_encode(const char *str) {
    size_t len = strlen(str);
    // 分配足够的内存来存储编码后的字符串，最多是原字符串的 3 倍
    char *encoded = malloc(len * 3 + 1);
    if (!encoded) {
        return NULL;  // 内存分配失败
    }

    char *ptr = encoded;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = str[i];

        // 如果字符是字母数字或者特殊字符，则直接复制
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            *ptr++ = c;
        } else {
            // 否则进行 URL 编码
            sprintf(ptr, "%%%02X", c);
            ptr += 3;
        }
    }
    *ptr = '\0';  // 末尾加上 '\0'
    return encoded;
}

void ssr_decode(char *p, size_t *alen) {
	char c = ':', end[] = "/?", sep[] = "=", sep_end[] = "&";
	char *ptr, *p_end, *p_current;
	char str[100] = {0}, pad[] = "===";
	size_t len = 0;
	//找到最后一个字符:
	ptr = strrchr(p, c);
	if (!ptr) return;
	p_current = ++ptr;
	p_end = strpbrk(ptr, end);
	if (!p_end) return;
	do {
		len = p_end - ptr;
		memcpy(str, ptr, len);
		str[len] = '\0';
		strncat(str, pad, (4 - len % 4) % 4);
		if (is_base64(str) == 0) {
			base64_decode(str, p_current, &len);
		}
		p_current += len;
		if (p_end != p + *alen && (ptr = strstr(p_end, sep)) != NULL) {
			memcpy(p_current, p_end, ptr - p_end + 1);
			p_current += ptr - p_end + 1;
		} else {
			break;
		}
		p_end = strpbrk(ptr++, sep_end);
		if (!p_end) p_end = p + *alen;
	} while (ptr);
	*p_current ='\0';
	*alen = (size_t)(p_current - p);
}

int main(int argc, char* argv[]) {
    CURL *curl_handle;
    CURLcode res;
    char* output = NULL;
    size_t output_length;
    char deli[] = "://", sep[] = "@#", pad[] = "===";
    char ss_file[] = "/tmp/ss_link";
    FILE *fp;

    if (argc == 1) {
	printf("Usage: %s [URL]\n", argv[0]);
	return 1;
    }

   // 初始化存储结构
    struct MemoryStruct chunk;
    chunk.memory = malloc(1);  // 初始分配
    chunk.size = 0;    // 初始大小

    // 全局libcurl初始化
    curl_global_init(CURL_GLOBAL_DEFAULT);

    // 初始化curl会话
    curl_handle = curl_easy_init();
    if(curl_handle) {
        // 设置URL
        curl_easy_setopt(curl_handle, CURLOPT_URL, argv[1]);

	curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYHOST, 0L);
	curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYPEER, 0L);

        // 设置回调函数和用户数据
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);

        // 执行请求
        res = curl_easy_perform(curl_handle);

        // 检查请求是否成功
        if(res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        } else {
	    if (!is_base64(chunk.memory)) {
		char *p, *line, *temp, *ptr, *data, *saveptr;
		size_t count = 0, decode_len = 0;
		output = (unsigned char*)malloc(chunk.size);
		base64_decode(chunk.memory, output, &output_length);
		output[output_length] = '\0';  // Null-terminate the output string
		memset(chunk.memory, 0, chunk.size);

		// memory for padding
		ptr = (char *)malloc(PAD_MEM);
		if (ptr == NULL) {
			printf("alloc fail\n");
		}

		// file operate
		fp=fopen(ss_file, "w+");
		if (fp) fprintf(fp, "["); //start

		// 逐行处理
		line = strtok_r(output, "\n", &saveptr);
		while (line) {
			p = strstr(line, deli);
			if (p) {
				p += 3;
				memcpy(chunk.memory + count, line, p - line);
				count += p - line;
				data = chunk.memory + count; //start of proto
				temp = strpbrk(p, sep);
				decode_len = temp ? temp - p : strlen(line) - (size_t)(p - line);
				if (!strncmp("trojan", line, 6)) { //trojan no need decode_base64
					memcpy(chunk.memory + count, p, decode_len);
					count += decode_len;
					p = temp;
					goto decode;
				}
				if (decode_len > PAD_MEM - 3) {
					ptr = realloc(ptr, calculate_length(decode_len) + 1);
					if (!ptr) break;
				}
				memcpy(ptr, p, decode_len);
				p += decode_len;
				ptr[decode_len] = '\0';
				if (decode_len % 4) {
					strncat(ptr, pad, 4 - decode_len % 4);
				}
				if (is_base64(ptr) == 0) {
					base64_decode(ptr, chunk.memory + count, &decode_len);
					if (!strncmp("ssr", line, 3)) {
						memcpy(ptr, chunk.memory + count, decode_len + 1);
						ssr_decode(ptr, &decode_len);
						memcpy(chunk.memory + count, ptr, decode_len + 1);
					}
					count += decode_len;
				}
decode:
				if (p && *p) {
					url_decode(p, ptr);
					memcpy(chunk.memory + count, ptr, strlen(ptr));
					count += strlen(ptr) + 1;
				} else {
					count ++;
				}
			}
			// json
			if (fp && !strncmp("ss", line, 2) && line[2] == ':') {
				fseek(fp, -1, SEEK_CUR);
				if (fgetc(fp) == '}') fprintf(fp, ",\n"); 
				strcpy(output, data);
				char ss[][10] = {"method", "password", "server", "port", "name"}, ss_deli[]=":@#";
				char *token, *ptr, *encoded_str;
				int i = 0;
				fprintf(fp, "{\"proto\":\"ss\""); //start
				token = strtok_r(output, ss_deli, &ptr);
				while (token != NULL) {
					encoded_str = url_encode(token);
					fprintf(fp, ",\"%s\":\"%s\"", ss[i++], encoded_str);
					if (encoded_str) {
						free(encoded_str);
						encoded_str = NULL;
					}
					token = strtok_r(NULL, ss_deli, &ptr);
				}
				fprintf(fp, "}"); //end
			}
			line = strtok_r(NULL, "\n", &saveptr);
			chunk.memory[count - 1] = (line ? '\n' : '\0');
		}
		if (fp) {
			fprintf(fp, "]\n"); //end
			fclose(fp);
		}
		printf("%s\n", chunk.memory);

		if (ptr) {
			free(ptr);
		}

		if (output) {
			free(output);
		}
	    }
        }

        // 清理curl句柄
        curl_easy_cleanup(curl_handle);
    }

    // 清理动态分配的内存
    if(chunk.memory) {
        free(chunk.memory);
    }

    // 全局libcurl清理
    curl_global_cleanup();

    return 0;
}

