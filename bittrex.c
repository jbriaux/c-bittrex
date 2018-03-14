/*
 * MIT License
 *
 * Copyright (c) 2018 Jean-Baptiste Riaux <jb.riaux@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <curl/curl.h>
#include <string.h>
#include <time.h>

#include "lib/jansson/src/jansson.h"
#include "bittrex.h"
#include "market.h"
#include "account.h"

/*
 * return simple nonce for api with key calls
 */
char *getnonce() {
	char *nonce;
	struct timeval tv;

	gettimeofday (&tv, NULL);
	nonce = malloc(10);
	nonce[0] = '\0';
	sprintf(nonce, "%d", (int)tv.tv_sec);
	return nonce;

}

/*
 * init struct bittrex_info
 */
struct bittrex_info *bittrex_info() {
	struct bittrex_info *bi;

	bi = malloc(sizeof(struct bittrex_info));
	if (!bi) {
		fprintf(stderr, "Could not allocate bittrex_info\n");
		return NULL;
	}
	bi->markets = NULL;
	bi->currencies = NULL;
	bi->api = NULL;
	bi->connector = NULL;
	bi->nbmarkets = 0;

	// this call is not thread safe, must be called only once
	curl_global_init(CURL_GLOBAL_ALL);

	return bi;
}

int conn_init(struct bittrex_info *bi) {
	if (!bi->connector) {
		bi->connector = mysql_init(NULL);
	} else {
		fprintf(stderr, "MySQL already initiated\n");
		return 0;
	}
	if (mysql_real_connect(bi->connector, "localhost", MYSQL_USER, MYSQL_PASSWD,
			       MYSQL_DB, 0, NULL, 0) == NULL) {
		fprintf(stderr, "%s\n", mysql_error(bi->connector));
		mysql_close(bi->connector);
		return 0;
	} else {
		printf("Connected to Database(OK)\n");
	}
	return 1;
}

void free_bi(struct bittrex_info *bi) {
	if (bi) {
		free_markets(bi->markets);
		free_currencies(bi->currencies);
		free_api(bi->api);
		free(bi);
	}
	curl_global_cleanup();
}

char *json_string_get(char *dest, json_t *tmp) {
	if (tmp && json_string_value(tmp)) {
		dest = malloc(strlen(json_string_value(tmp))+1);
		dest = strcpy(dest, json_string_value(tmp));
		return dest;
	}
	return NULL;
}

double json_real_get(json_t *tmp) {
	if (tmp && json_real_value(tmp)) {
		return json_real_value(tmp);
	}
	return 0;
}

size_t write_response(void *ptr, size_t size, size_t nmemb, void *stream) {
    struct write_result *result = (struct write_result *)stream;

    if(result->pos + size * nmemb >= BUFFER_SIZE - 1)
    {
        fprintf(stderr, "error: too small buffer\n");
        return 0;
    }

    memcpy(result->data + result->pos, ptr, size * nmemb);
    result->pos += size * nmemb;

    return size * nmemb;
}


char *request(const char *url)
{
    CURL *curl = NULL;
    CURLcode status;
    char *data = NULL;
    long code;

    curl = curl_easy_init();
    if(!curl)
        goto error;

    data = malloc(BUFFER_SIZE);
    if(!data)
        goto error;

    struct write_result write_result = {
        .data = data,
        .pos = 0
    };

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_response);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &write_result);

    status = curl_easy_perform(curl);
    if(status != 0)
    {
        fprintf(stderr, "error: unable to request data from %s:\n", url);
        fprintf(stderr, "%s\n", curl_easy_strerror(status));
        goto error;
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    if(code != 200)
    {
        fprintf(stderr, "error: server responded with code %ld\n", code);
        goto error;
    }

    curl_easy_cleanup(curl);

    /* zero-terminate the result */
    data[write_result.pos] = '\0';

    return data;

error:
    if(data)
	    free(data);
    if(curl)
	    curl_easy_cleanup(curl);
    return NULL;
}

char *apikey_request(const char *url, char *hmac)
{
    CURL *curl = NULL;
    CURLcode status;
    char *data = NULL;
    long code;

    curl = curl_easy_init();
    if(!curl)
        goto error;

    data = malloc(BUFFER_SIZE);
    if(!data)
        goto error;

    struct write_result write_result = {
        .data = data,
        .pos = 0
    };

    curl_easy_setopt(curl, CURLOPT_URL, url);
    struct curl_slist *headers=NULL;
    char *tmpbuf = malloc(strlen("apisign:")+strlen(hmac)+1);
    tmpbuf[0] =  '\0';
    tmpbuf = strcat(tmpbuf, "apisign:");
    tmpbuf = strcat(tmpbuf, hmac);
    headers = curl_slist_append(headers, "Content-Type: ");
    headers = curl_slist_append(headers, tmpbuf);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_response);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &write_result);

    status = curl_easy_perform(curl);
    if(status != 0)
    {
        fprintf(stderr, "error: unable to request data from %s:\n", url);
        fprintf(stderr, "%s\n", curl_easy_strerror(status));
        goto error;
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    if(code != 200)
    {
        fprintf(stderr, "error: server responded with code %ld\n", code);
        goto error;
    }

    curl_easy_cleanup(curl);

    /* zero-terminate the result */
    data[write_result.pos] = '\0';

    return data;

error:
    if(data)
        free(data);
    if(curl)
        curl_easy_cleanup(curl);
    return NULL;
}


/*
 * Call to bittrex API
 * Check success field
 * return null on error or ptr on json_t result
 *
 * Sometimes API replies with success true but
 * result array or string is empty.
 * We replay the call until it returns normal output.
 * program could be blocked here in infinite recursion
 * if API keeps replying crap.
 *
 */
json_t *api_call(char *call) {
	json_t *root, *result, *tmp;
	json_error_t error;
	char *reply;

	reply = request(call);

	if(!reply)
		return NULL;

	root = json_loads(reply, 0, &error);
	free(reply);

	if(!root)
	{
		fprintf(stderr, "error: on line %d: %s\n", error.line, error.text);
		return NULL;
	}

	result = json_object_get(root,"success");
	if (json_typeof(result) != JSON_TRUE) {
		fprintf(stderr, "Error proccessing request: %s\n", call);
		tmp = json_object_get(root, "message");
		if (tmp && json_string_value(tmp))
			printf("API replied: %s\n", json_string_value(tmp));
		return NULL;
	}

	result = json_object_get(root,"result");
	if ((json_typeof(result) == JSON_ARRAY) &&  (json_array_size(result) == 0)) {
		fprintf(stderr, "Error proccessing request: %s, result field(array) empty. ", call);
		fprintf(stderr, "Retrying\n");
		json_decref(root);
		return api_call(call);
	}
	if ((json_typeof(result) == JSON_STRING) && (strlen(json_string_value(result)) == 0)) {
		fprintf(stderr, "Error proccessing request: %s, result field(string) empty. ", call);
		fprintf(stderr, "Retrying\n");
		json_decref(root);
		return api_call(call);
	}

	return root;
}

/*
 * Call to bittrex API with API key
 * Check success field
 * return null on error or ptr on json_t result
 *
 * Unlike api_call() we do not replay call on failures.
 */
json_t *api_call_sec(char *call, char *hmac) {
	json_t *root, *result, *tmp;
	json_error_t error;
	char *reply;

	reply = apikey_request(call, hmac);

	if(!reply)
		return NULL;

	//printf("%s\n", reply);
	root = json_loads(reply, 0, &error);
	free(reply);

	if(!root)
	{
		fprintf(stderr, "error: on line %d: %s\n", error.line, error.text);
		return NULL;
	}

	result = json_object_get(root,"success");
	if (json_typeof(result) != JSON_TRUE) {
		fprintf(stderr, "Error proccessing request: %s\n", call);
		tmp = json_object_get(root, "message");
		if (tmp && json_string_value(tmp))
			printf("API replied: %s\n", json_string_value(tmp));
		return NULL;
	}

	return root;
}
