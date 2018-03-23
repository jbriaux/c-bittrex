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

#ifndef BITTREX_H
#define BITTREX_H

#include <mysql/mysql.h>

#include "lib/jansson/src/jansson.h"

// do not use (won't work anyway), this is for history
#define API_URL_V1 "https://bittrex.com/api/v1/"

// current API
#define API_URL_V11 "https://bittrex.com/api/v1.1/"

// future API see https://github.com/dparlevliet/node.bittrex.api#supported-v2-api-methods
#define API_URL_V2 "https://bittrex.com/api/v2.0/"

// current default API is V1.1
#define API_URL API_URL_V11

/*
 * Please refer to https://bittrex.com/home/api for details
 *
 *  Public - Public information available without an API key
 *  Account - For managing your account
 *  Market - For programatic trading of crypto currencies
 */
#define PUBLIC_API_URL API_URL "public/"
#define MARKET_API_URL API_URL "market/"
#define ACCOUNT_API_URL API_URL "account/"

#define PUBLIC_API2_URL API_URL_V2 "pub/"
#define MARKET_API2_URL API_URL_V2 "pub/market/"

#define GETMARKETS PUBLIC_API_URL "getmarkets"
#define GETMARKETSUMMARY PUBLIC_API_URL "getmarketsummary?market="
#define GETMARKETSUMMARIES PUBLIC_API_URL "getmarketsummaries"
#define GETMARKETHISTORY PUBLIC_API_URL "getmarkethistory?market="
#define GETORDERBOOK PUBLIC_API_URL "getorderbook?market="
#define GETTICKER PUBLIC_API_URL "getticker?market="
#define GETCURRENCIES PUBLIC_API_URL "getcurrencies"
#define GETTICKS MARKET_API2_URL "GetTicks?marketName="
#define BUYLIMIT MARKET_API_URL "buylimit?apikey="
#define SELLLIMIT MARKET_API_URL "selllimit?apikey="
#define CANCELORDER MARKET_API_URL "cancel?apikey="
#define GETOPENORDERS MARKET_API_URL "getopenorders?apikey="

#define TRADEBUY API_URL_V2  /* fixme */
#define TRADESELL API_URL_V2  /* fixme */

#define GETBALANCE ACCOUNT_API_URL "getbalance?apikey="
#define GETBALANCES ACCOUNT_API_URL "getbalances?apikey="
#define GETDEPOSITADDRESS ACCOUNT_API_URL "getdepositaddress?apikey="
#define GETDEPOSITHISTORY ACCOUNT_API_URL "getdeposithistory?apikey="
#define WITHDRAW ACCOUNT_API_URL "withdraw?apikey="
#define GETORDER ACCOUNT_API_URL "getorder?apikey="
#define GETORDERHISTORY ACCOUNT_API_URL "getorderhistory?apikey="
#define GETWITHDRAWALHISTORY ACCOUNT_API_URL "getwithdrawalhistory?apikey="

#define BUFFER_SIZE  (4096 * 1024)  /* 4MB  => this is way too much 1MB should be ok */
#define MAX_ACTIVE_MARKETS 5 /* for the bot */

#define MYSQL_PASSWD "Whr3PvCJ7cb"
#define MYSQL_DB "bbot"
#define MYSQL_USER "bbot_user"

struct write_result
{
    char *data;
    int pos;
};

struct bittrex_info {
	struct market **markets;
	struct currency **currencies;
	struct api *api;
	/* keep track of the number of struct market, for qsort */
	int nbmarkets;
	MYSQL *connector;
	/* Lock for MySQL, API calls, and modifs from different threads */
	pthread_mutex_t bi_lock;
	/* Last call to API (time) */
	time_t lastcall_t;
	/* Last call to API (request) : 1/s max*/
	char *lastcall;
};

struct bittrex_info *bittrex_info();

/*
 * init connection to MySQL
 */
int conn_init(struct bittrex_info *bi);

/*
 * json custom getter
 */
double json_real_get(json_t *tmp);
char *json_string_get(char *dest, json_t *tmp);

/*
 * fixme : do a single api call function
 */
char *request(const char *url);
json_t *api_call(struct bittrex_info *bi, char *call, char *rootcall);
json_t *api_call_sec(struct bittrex_info *bi, char *call, char *hmac, char *rootcall);
char *getnonce();

/*
 * free everything in bittrex_info
 */
void free_bi(struct bittrex_info *bi);

#endif
