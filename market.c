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

#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

#include "lib/jansson/src/jansson.h"

#include "market.h"
#include "bittrex.h"
#include "trade.h"
#include "account.h"

int compare_market_by_volume(const void *a, const void *b) {
	struct market **ma = (struct market **)a;
	struct market **mb = (struct market **)b;
	double diff = 0;

	if (ma && mb && *ma && *mb) {
		diff = (*ma)->basevolume - (*mb)->basevolume;
		if (diff > 0)
			return -1;
		if (diff < 0)
			return 1;
	}
	return 0;
}

int market_exists(struct market **mtab, char *marketname) {
	struct market **tmp = mtab;

	while (*tmp) {
		if (strcmp((*tmp)->marketname, marketname) == 0)
			return 0;
		tmp++;
	}
	return -1;
}

struct market *new_market() {
	struct market *m = NULL;

	if (!(m = malloc(sizeof(struct market)))) {
		return NULL;
	}
	m->mh = NULL;
	m->ms = NULL;
	m->ob = NULL;
	m->marketname = NULL;
	m->marketcurrency = NULL;
	m->marketcurrencylong = NULL;
	m->basecurrency = NULL;
	m->basecurrencylong = NULL;
	m->rsi = 0;
	m->brsi = 0;
	m->macd = 0;

	pthread_mutex_init(&(m->indicators_lock), NULL);

	return m;
}

int getmarkets(struct bittrex_info *bi) {
	int i = 0;
	json_t *result, *raw, *market_name, *root, *tmp;
	struct market *m = NULL;


	root = api_call(GETMARKETS);
	if (!root)
		return 0;

	result = json_object_get(root,"result");
	if (!json_is_array(result)) {
		fprintf(stderr, "getmarkets: API returned not an array");
		return 0;
	}

	bi->markets = malloc((json_array_size(result)+1) * sizeof(struct market*));

	for (i = 0; (unsigned) i < json_array_size(result); i++) {
		raw = json_array_get(result, i);
		m = new_market();

		market_name = json_object_get(raw, "MarketName");
		m->marketname = json_string_get(m->marketname, market_name);

		tmp = json_object_get(raw, "MarketCurrency");
		m->marketcurrency = json_string_get(m->marketcurrency, tmp);

		tmp = json_object_get(raw, "MarketCurrencyLong");
		m->marketcurrencylong = json_string_get(m->marketcurrencylong, tmp);

		tmp = json_object_get(raw, "BaseCurrency");
		m->basecurrency = json_string_get(m->basecurrency, tmp);

		tmp = json_object_get(raw, "BaseCurrencyLong");
		m->basecurrencylong = json_string_get(m->basecurrencylong, tmp);

		tmp = json_object_get(raw, "IsActive");
		m->isactive = json_is_true(tmp);

		tmp = json_object_get(raw, "MinTradeSize");
		m->mintradesize = json_real_value(tmp);

		bi->markets[i] = m;
	}
	bi->markets[i] = NULL;
	bi->nbmarkets = i - 1;
	json_decref(root);
	return i-1;
}

struct market *getmarket(struct market **markets, char *marketname) {
	struct market **tmp;

	if (!markets || !marketname)
		return NULL;
	tmp = markets;

	while (tmp && *tmp) {
		if (strcmp((*tmp)->marketname, marketname) == 0)
			return *tmp;
		tmp++;
	}
	return NULL;
}

struct ticker *getticker(struct market *m) {
	json_t *root, *result;
	char *url;
	struct ticker *ticker;

	if (!m || !m->marketname) {
		fprintf(stderr, "getticker: invalid market specified.\n");
		return NULL;
	}

	url = malloc((strlen(GETTICKER)+strlen(m->marketname)+1)*sizeof(char));
	url[0]='\0';
	url = strcat(url, GETTICKER);
	url = strcat(url, m->marketname);

	root = api_call(url);
	if (!root)
		return NULL;
	result = json_object_get(root,"result");

	ticker = malloc(sizeof(struct ticker));
	ticker->bid = json_real_value(json_object_get(result, "Bid"));
	ticker->ask = json_real_value(json_object_get(result, "Ask"));
	ticker->last = json_real_value(json_object_get(result, "Last"));

	json_decref(root);
	//printf("%.8f %.8f %.8f\n", ticker->bid, ticker->ask, ticker->last);

	return ticker;
}

/*
 * getticks
 * API returns old ticks to new ticks (result[0] is oldest)
 * Here ticks are sorted reverse: new to old
 */
struct tick **getticks(struct market *m, char *interval, int nbtick) {
	json_t *root, *result, *raw;
	char *url;
	int size,i;
	struct tick **ticks, *tick;

	if (!m || !m->marketname) {
		fprintf(stderr, "getticker: invalid market specified.\n");
		return NULL;
	}

	url = malloc((strlen(GETTICKS)+strlen(m->marketname)+
		      strlen(interval)+strlen("&tickInterval=")+1)*sizeof(char));
	url[0]='\0';
	url = strcat(url, GETTICKS);
	url = strcat(url, m->marketname);
	url = strcat(url, "&tickInterval=");
	url = strcat(url, interval);

	root = api_call(url);
	if (!root)
		return NULL;

	result = json_object_get(root,"result");

	size = json_array_size(result);
	if (size == 0)
		return NULL;

	ticks = malloc((nbtick+1) * sizeof(struct tick*));

	for (i=0; i < size && i < nbtick; i++) {
		raw = json_array_get(result, size-i-1);
		tick = malloc(sizeof(struct tick));
		tick->open = json_real_value(json_object_get(raw, "C"));
		tick->high = json_real_value(json_object_get(raw, "H"));
		tick->low = json_real_value(json_object_get(raw, "L"));
		tick->close = json_real_value(json_object_get(raw, "C"));
		tick->volume = json_real_value(json_object_get(raw, "V"));
		tick->btcval = json_real_value(json_object_get(raw, "BV"));
		tick->timestamp = json_string_get(tick->timestamp, json_object_get(raw, "T"));
		ticks[i] = tick;
	}
	ticks[i] = NULL;

	json_decref(root);
	free(url);

	return ticks;
}

struct currency *getcurrency(struct currency **currency, char *coin) {
	struct currency **tmp;

	if (currency && *currency && coin) {
		tmp = currency;
		while (*tmp) {
			if (strcmp((*tmp)->coin, coin) == 0)
				return (*tmp);
			tmp++;
		}
	}
	return NULL;
}

struct currency **getcurrencies() {
	struct currency *c, **currencies;
	json_t *root, *result, *tmp, *raw;
	int size=0, i;

	root = api_call(GETCURRENCIES);
	if (!root)
		return NULL;

	result = json_object_get(root,"result");
	if (!json_is_array(result)) {
		fprintf(stderr, "getcurrencies: API returned not an array");
		return NULL;
	}
	size = json_array_size(result);
	currencies = malloc((size+1) * sizeof(struct currency*));

	for (i=0; i < size; i++) {
		raw = json_array_get(result, i);
		c = malloc(sizeof(struct currency));

		tmp = json_object_get(raw, "Currency");
		c->coin = json_string_get(c->coin, tmp);

		tmp = json_object_get(raw, "CurrencyLong");
		c->currencylong = json_string_get(c->currencylong, tmp);

		tmp = json_object_get(raw, "MinConfirmation");
		c->minconfirmation = json_integer_value(tmp);

		tmp = json_object_get(raw, "TxFee");
		c->txfee = json_real_value(tmp);

		tmp = json_object_get(raw, "IsActive");
		c->isactive = json_is_true(tmp);

		tmp = json_object_get(raw, "CoinType");
		c->cointype = json_string_get(c->cointype, tmp);

		tmp = json_object_get(raw, "BaseAddress");
		c->baseaddress = json_string_get(c->baseaddress, tmp);

		currencies[i] = c;
	}
	currencies[i] = NULL;
	json_decref(root);

	return currencies;
}

/*
 * get market summaries, sorted by volume
 */
int getmarketsummaries(struct bittrex_info *bi){
	struct market *m = NULL;
	struct tm *ctm = NULL;
	int i = 0;
	json_t *result, *raw, *market_name, *root, *tmp;

	root = api_call(GETMARKETSUMMARIES);
	if (!root)
		return -1;

	result = json_object_get(root,"result");
	if (!json_is_array(result)) {
		fprintf(stderr, "getmarkersummaries: API returned not an array");
		return -1;
	}
	if (!bi->markets)
		getmarkets(bi);

	for (i = 0; (unsigned) i < json_array_size(result); i++) {
		raw = json_array_get(result, i);
		market_name = json_object_get(raw, "MarketName");
		m = getmarket(bi->markets,(char*)json_string_value(market_name));

		if (m) {
			if (!m->ms) {
				m->ms = malloc(sizeof(struct market_summary));
				m->ms->timestamp = NULL;
				m->ms->ctm = NULL;
			}

			tmp = json_object_get(raw, "TimeStamp");

			/* free if update only*/
			if (m->ms->timestamp)
				free(m->ms->timestamp);

			m->ms->timestamp = malloc(strlen(json_string_value(tmp))+1);
			m->ms->timestamp = strcpy(m->ms->timestamp, json_string_value(tmp));
			ctm = malloc(sizeof(struct tm));
			sscanf(json_string_value(tmp), "%d-%d-%dT%d:%d:%d", &ctm->tm_year,
			       &ctm->tm_mon, &ctm->tm_mday, &ctm->tm_hour,
			       &ctm->tm_min, &ctm->tm_sec);

			/* free if update only*/
			if (m->ms->ctm)
				free(m->ms->ctm);

			m->ms->ctm = ctm;

			tmp = json_object_get(raw, "Last");
			m->ms->last = json_real_value(tmp);

			tmp = json_object_get(raw, "High");
			m->ms->high = json_real_value(tmp);

			tmp = json_object_get(raw, "Low");
			m->ms->low = json_real_value(tmp);

			tmp = json_object_get(raw, "BaseVolume");
			m->ms->basevolume = json_real_value(tmp);
			m->basevolume = json_real_value(tmp); //fixme basevolume only in MS

			tmp = json_object_get(raw, "Volume");
			m->ms->volume = json_real_value(tmp);
			m->volume = json_real_value(tmp);

			tmp = json_object_get(raw, "Bid");
			m->ms->bid = json_real_value(tmp);

			tmp = json_object_get(raw, "Ask");
			m->ms->ask = json_real_value(tmp);

			tmp = json_object_get(raw, "OpenBuyOrders");
			m->ms->openb = json_number_value(tmp);

			tmp = json_object_get(raw, "OpenSellOrders");
			m->ms->opens = json_number_value(tmp);

			tmp = json_object_get(raw, "PrevDay");
			m->ms->prevday = json_real_value(tmp);
		}
	}
	json_decref(root);
	qsort(bi->markets, bi->nbmarkets, sizeof(struct market*), compare_market_by_volume);
	return 0;
}

/*
 * get market history (last 100)
 */
int getmarkethistory(struct market *m) {
	json_t *root, *result, *tmp, *raw;
	int size, i;
	char *url;
	struct tm  *ctm;

	if (!m || !m->marketname) {
		fprintf(stderr, "getmarkethistory: invalid market specified.\n");
		return -1;
	}

	url = malloc((strlen(GETMARKETHISTORY)+strlen(m->marketname)+1)*sizeof(char));
	url[0]='\0';
	url = strcat(url, GETMARKETHISTORY);
	url = strcat(url, m->marketname);

	root = api_call(url);
	if (!root)
		return -1;

	result = json_object_get(root,"result");
	if (!json_is_array(result)) {
		fprintf(stderr, "getmarkethistory: API returned not an array");
		return -1;
	}
	size = json_array_size(result);
	/* alloc only first call */
	if (!m->mh) {
		m->mh = malloc((size+1) * sizeof(struct market_history*));
		for (i=0; i < size; i++) {
			m->mh[i] = malloc(sizeof(struct market_history));
			m->mh[i]->timestamp = NULL;
			m->mh[i]->ctm = NULL;
			m->mh[i]->filltype = NULL;
			m->mh[i]->ordertype = NULL;
		}
	}

	for (i=0; i < size; i++) {
		raw = json_array_get(result, i);

		tmp = json_object_get(raw, "Id");
		m->mh[i]->id = json_number_value(tmp);


		tmp = json_object_get(raw, "TimeStamp");
		/* free old timestamp */
		if (m->mh[i] && m->mh[i]->timestamp)
			free(m->mh[i]->timestamp);
		if (json_string_value(tmp)) {
			m->mh[i]->timestamp = malloc(strlen(json_string_value(tmp))+1);
			m->mh[i]->timestamp = strcpy(m->mh[i]->timestamp, json_string_value(tmp));
			ctm = malloc(sizeof(struct tm));
			sscanf(json_string_value(tmp), "%d-%d-%dT%d:%d:%d", &ctm->tm_year,
			       &ctm->tm_mon, &ctm->tm_mday, &ctm->tm_hour,
			       &ctm->tm_min, &ctm->tm_sec);
		}
		/* free if update only*/
		if (m->mh[i]->ctm)
			free(m->mh[i]->ctm);
		m->mh[i]->ctm = ctm;

		tmp = json_object_get(raw, "Price");
		m->mh[i]->price = json_real_value(tmp);

		tmp = json_object_get(raw, "Quantity");
		m->mh[i]->quantity = json_real_value(tmp);

		tmp = json_object_get(raw, "Total");
		m->mh[i]->total = json_real_value(tmp);

		tmp = json_object_get(raw, "FillType");
		m->mh[i]->filltype = json_string_get(m->mh[i]->filltype, tmp);

		tmp = json_object_get(raw, "OrderType");
		m->mh[i]->ordertype = json_string_get(m->mh[i]->ordertype, tmp);

	}
	m->mh[i] = NULL;

	json_decref(root);
	free(url);

	return 0;
}

int getmarketsummary(struct market *m) {
	json_t *root, *result, *tmp, *raw;
	char *url;
	struct tm  *ctm;

	if (!m || !m->marketname) {
		fprintf(stderr, "getmarketsummary: invalid market specified.\n");
		return -1;
	}

	url = malloc((strlen(GETMARKETSUMMARY)+strlen(m->marketname)+1)*sizeof(char));
	url[0]='\0';
	url = strcat(url, GETMARKETSUMMARY);
	url = strcat(url, m->marketname);

	root = api_call(url);
	if (!root)
		return -1;

	result = json_object_get(root,"result");
	if (!json_is_array(result)) {
		fprintf(stderr, "getmarketsummary: API returned not an array");
		return -1;
	}

	/* alloc only first call */
	if (!m->ms) {
		m->ms = malloc(sizeof(struct market_summary));
		m->ms->timestamp = NULL;
		m->ms->ctm = NULL;
	}

	raw = json_array_get(result, 0);
	tmp = json_object_get(raw, "TimeStamp");

	if (!m->ms->timestamp)
		m->ms->timestamp = malloc(strlen(json_string_value(tmp))+1);

	m->ms->timestamp = strcpy(m->ms->timestamp, json_string_value(tmp));
	ctm = malloc(sizeof(struct tm));
	sscanf(json_string_value(tmp), "%d-%d-%dT%d:%d:%d", &ctm->tm_year,
	       &ctm->tm_mon, &ctm->tm_mday, &ctm->tm_hour,
	       &ctm->tm_min, &ctm->tm_sec);

	/* free if update only*/
	if (m->ms->ctm)
		free(m->ms->ctm);

	m->ms->ctm = ctm;

	tmp = json_object_get(raw, "Last");
	m->ms->last = json_real_value(tmp);

	tmp = json_object_get(raw, "High");
	m->high = m->ms->high = json_real_value(tmp);

	tmp = json_object_get(raw, "Low");
	m->low = m->ms->low = json_real_value(tmp);

	tmp = json_object_get(raw, "BaseVolume");
	m->basevolume = m->ms->basevolume = json_real_value(tmp);

	tmp = json_object_get(raw, "Volume");
	m->ms->volume = json_real_value(tmp);

	tmp = json_object_get(raw, "Bid");
	m->ms->bid = json_real_value(tmp);

	tmp = json_object_get(raw, "Ask");
	m->ms->ask = json_real_value(tmp);

	tmp = json_object_get(raw, "OpenBuyOrders");
	m->ms->openb = json_number_value(tmp);

	tmp = json_object_get(raw, "OpenSellOrders");
	m->ms->opens = json_number_value(tmp);

	tmp = json_object_get(raw, "PrevDay");
	m->ms->prevday = json_real_value(tmp);

	json_decref(root);
	free(url);

	return 0;
}

static struct order **getorders(json_t *tab, int size) {
	struct order **o;
	json_t *tmp, *raw;
	int i = 0;

	o = malloc((size+1) * sizeof(struct order*));
	for (i=0; i < size; i++) {
		o[i] = malloc(sizeof(struct order));
		raw = json_array_get(tab, i);
		tmp = json_object_get(raw, "Quantity");
		o[i]->quantity = json_real_value(tmp);
		tmp = json_object_get(raw, "Rate");
		o[i]->rate = json_real_value(tmp);
	}
	o[i] = NULL;
	return o;
}

/*
 * Get Orderbook for given market.
 * Type must be specified: buy, sell or both
 */
int getorderbook(struct market *m, char *type) {
	json_t *root, *result, *tmp;
	int size;
	char *url;

	if (!m || !m->marketname) {
		fprintf(stderr, "getorderbook: invalid market specified.\n");
		return -1;
	}

	if (!strcmp(type, "both") && !strcmp(type, "buy") && !strcmp(type, "sell")) {
		fprintf(stderr, "getorderbook: invalid type specified.\n");
		fprintf(stderr, "getorderbook: valid types \"both, buy, sell\".\n");
		return -1;
	}

	url = malloc((strlen(GETORDERBOOK)+
		      strlen(m->marketname)+
		      strlen("&type=")+
		      strlen(type)+1)*sizeof(char));
	url[0]='\0';
	url = strcat(url, GETORDERBOOK);
	url = strcat(url, m->marketname);
	url = strcat(url, "&type=");
	url = strcat(url, type);
	root = api_call(url);
	if (!root)
		return -1;

	if (!m->ob) {
		m->ob = malloc(sizeof(struct orderbook));
		m->ob->buy = NULL;
		m->ob->sell = NULL;
	}

	if (strcmp(type, "both") == 0) {
		result = json_object_get(root,"result");
		tmp =  json_object_get(result, "buy");
		if (!json_is_array(tmp)) {
			fprintf(stderr, "getorderbook (both 1/2, buy): API returned not an array");
			return -1;
		}
		size = json_array_size(tmp);
		if (!m->ob->buy)
			m->ob->buy = malloc((size+1)*sizeof(struct order*));

		m->ob->buy = getorders(tmp, size);

		tmp =  json_object_get(result, "sell");
		if (!json_is_array(tmp)) {
			fprintf(stderr, "getorderbook (both 2/2, sell): API returned not an array");
			return -1;
		}
		size = json_array_size(tmp);
		if (!m->ob->sell)
			m->ob->sell = malloc((size+1)*sizeof(struct order*));
		m->ob->sell = getorders(tmp, size);


	} else {
		tmp =  json_object_get(root, "result");
		if (!json_is_array(tmp)) {
			fprintf(stderr, "getorderbook (%s): API returned not an array", type);
			return -1;
		}
		size = json_array_size(tmp);
		if (strcmp("buy", type) == 0) {
			m->ob->buy = getorders(tmp, size);
			m->ob->sell = NULL;
		} else {
			m->ob->sell = getorders(tmp, size);
			m->ob->buy = NULL;
		}

	}

	json_decref(root);
	free(url);

	return 0;
}

/*
 * Return the sum of an array elements
 */
static double sum(double *tab, int size) {
	double total = 0;
	int i = 0;

	for (i=0; i < size; i++) {
		total += tab[i];
	}
	return total;
}

/*
 * Init double array of given size to 0
 */
static void tabinit(double *tab, int size) {
	int i = 0;

	for (i=0; i < size; i++) {
		tab[i] = 0;
	}
}

// for debug purposes
static void printtab(double *tab, int size) {
	int i = 0;

	printf("-----------------\n");
	for (i=0; i < size; i++) {
		printf("tab[%d]: %.8f\n", i, tab[i]);
	}
}

/*
 * we insert so state is always pending (and we don't want 7 args, does not fit in registers)
 * the quantity put in base is the real quantity baught without the fees
 * I buy 100BTC, we insert in base 99.75
 */
static int insert_order(MYSQL *connector, char *UUID, char *mname, double qty, double rate, double btcpaid) {
	char *query;
	char qtystr[32], ratestr[32], btcpaidstr[32];
	int query_status;

	sprintf(qtystr, "%.8f", qty);
	sprintf(ratestr, "%.8f", rate);
	sprintf(btcpaidstr, "%.8f", btcpaid);
	query = malloc(strlen("INSERT INTO Orders (UUID,Market,Quantity,Rate,BotType,BotState,BtcPaid) ") +
		       strlen("VALUES (") +
		       strlen(UUID) + 3 +
		       strlen(mname) + 3 +
		       strlen(qtystr) + 3 +
		       strlen(ratestr) + 3 +
		       strlen("'buy'") +
		       strlen("pending") + 3 +
		       strlen(btcpaidstr) + 3 +
		       strlen(");") + 1);
	query[0] = '\0';
	query = strcat(query, "INSERT INTO Orders (UUID,Market,Quantity,Rate,BotType,BotState,BtcPaid) ");
	query = strcat(query, "VALUES (");
	query = strcat(query, "'"); query = strcat(query, UUID); query = strcat(query, "',");;
	query = strcat(query, "'"); query = strcat(query, mname); query = strcat(query, "',");
	query = strcat(query, "'"); query = strcat(query, qtystr); query = strcat(query, "',");
	query = strcat(query, "'"); query = strcat(query, ratestr); query = strcat(query, "',");
	query = strcat(query, "'buy',");
	query = strcat(query, "'pending',");
	query = strcat(query, "'"); query = strcat(query, btcpaidstr); query = strcat(query, "'");
	query = strcat(query, ");");

	query_status = mysql_query(connector, query);
	if (query_status != 0) {
		fprintf(stderr, "something went wrong when trying to insert: %s\n", query);
	}
	free(query);
	return query_status;
}

static int processed_order(MYSQL *connector, char *UUID, double gain) {
	char *query;
	char gainstr[18];
	int query_status;

	sprintf(gainstr, "%.8f", gain);
	query = malloc(strlen("UPDATE Orders SET BotState = 'processed', Gain = '' WHERE UUID='") +
		       strlen(gainstr) + strlen(UUID) + 3); // "'"+";"+"\0"
	query[0] = '\0';
	query = strcat(query, "UPDATE Orders SET BotState = 'processed',");
	query = strcat(query, "Gain = '");
	query = strcat(query, gainstr);
	query = strcat(query, "'");
	query = strcat(query, " WHERE UUID='");
	query = strcat(query, UUID);
	query = strcat(query, "';");
	printf("%s\n", query);

	query_status = mysql_query(connector, query);
	if (query_status != 0) {
		fprintf(stderr, "Something went wrong when trying to update: %s\n", query);
	}
	free(query);
	return query_status;
}

/*
 * For resuming the bot.
 * If an order is not marked as processed in database, returns it otherwise return NULL
 * 1 thread = 1 order max opened so number of row if any is 1
 */
static struct trade *lasttransaction(MYSQL *connector, struct market *m) {
	char *query, *buffer;
	int query_status;
	double qty, rate, btcpaid;
	MYSQL_RES *result;
	unsigned long *len;
	struct trade *t = NULL;

	query = malloc(strlen("SELECT * FROM Orders WHERE BotState = 'pending' AND Market = '';") +
		       strlen(m->marketname) + 1 );

	query[0] = '\0';
	query = strcat(query, "SELECT * FROM Orders WHERE BotState = 'pending' AND Market = '");
	query = strcat(query, m->marketname);
	query = strcat(query, "';");

	query_status = mysql_query(connector, query);
	if (query_status != 0)
		return NULL;

	result = mysql_store_result(connector);

	if (result && mysql_num_rows(result) == 1) {
		MYSQL_ROW row;

		buffer = malloc(42 * sizeof(char));
		row = mysql_fetch_row(result);
		len = mysql_fetch_lengths(result);

		buffer[0] = '\0';
		strncat(buffer, row[3], len[3] + 1);
		sscanf(buffer, "%lf", &qty);

		buffer[0] = '\0';
		strncat(buffer, row[4], len[4] + 1);
		sscanf(buffer, "%lf", &rate);

		buffer[0] = '\0';
		strncat(buffer, row[7], len[7] + 1);
		sscanf(buffer, "%lf", &btcpaid);

		t = new_trade(m, LIMIT, qty, rate, IMMEDIATE_OR_CANCEL, NONE, 0, BUY);

		t->realqty = qty;
		t->btcpaid = btcpaid;
		t->fee = ((0.25/100) * t->realqty * rate)/(1-(0.25/100*rate));
		free(buffer);
	}
	if (result)
		mysql_free_result(result);
	free(query);
	return t;
}

// fixme add stop loss option
// fixme add retry tick  if one tick is empty (API returned no value or timedout)
// if too many ticks are lost the API may be down: decide what to do

/*
 * Hardcoded RSI 14 strategy for specified market
 * One thread / market
 *
 * Buy when Wilder RSI is less than 30
 * Sell when Wilder RSI is greater than 70 and margin > 0.25% (fees)
 *
 * Two RSI are computed:
 * - rsi based on
 *   inc: increase of value (tick(N) - tick(N-1))
 *   dec: decrease of value
 *   formula used: sum(inc, 14)/(sum(inc,14)+sum(dec,14))
 *
 * - rsi of Wilder based on arithmetic moving average:
 *   mmi: moving average of value increase
 *        mmi[i] = (inc[i-1] + mmi[i-1]*(14-1))/14
 *   mmd: moving average of value decrease
 *        mmd[i] = (dec[i-1] + mmd[i-1]*(14-1))/14
 *   formula used:  1-(1/(1+mmi[i]/mmd[i]))
 */
void *indicators(void *b) {
	struct bittrex_bot *bbot = (struct bittrex_bot *)b;
	struct market *m = bbot->market;
	struct trade *buy=NULL, *sell=NULL;
	struct tick **ticks, **t;
	struct ticker *last, *tmptick;
	char *uuid = NULL;
	time_t begining;
	double val[14], inc[14], dec[14]; // for old rsi
	double mmi[14], mmd[14]; // for wielder rsi
	double ema9[9]; // EMA 9
	double ema14[14]; // EMA 14
	double ema28[28]; // EMA 28
	double w9 = (2.0/(9+1));
	double w14 = (2.0/(14+1));
	double w28 = (2.0/(28+1));
	double btcqty = 0, qty = 0;
	double tmp = 0, tmprsi = 0;
	int i = 0, j = 0, k = 0;
	int previous = 0;
	double testuuid = 4242;
	char testuuidstr[40];
	double previousloss = 0;

	// look for past unprocessed trades
	pthread_mutex_lock(&(bbot->bi->sql_lock));
	buy = lasttransaction(bbot->bi->connector, m);
	pthread_mutex_unlock(&(bbot->bi->sql_lock));

	// init tabs to 0
	printf("Started bot for market: %s\n", m->marketname);
	tabinit(val, 14);
	tabinit(inc, 14);
	tabinit(dec, 14);
	tabinit(mmi, 14);
	tabinit(mmd, 14);

	// Gather 14mn of history
	ticks = getticks(m, "oneMin", 14);
	for (i=13; i >= 0; i--) {
		val[i] = ticks[13-i]->close;
		ema14[i] =  val[i];
		if (i <= 8)
			ema9[i] = val[i];
	}
	free_ticks(ticks);

	// Rest of init i=0 will be filled later(first tick in while loop above)
	for (i=1; i <= 13; i++) {
		tmp = val[i] - val[i-1];

		if (tmp >= 0) { //inc
			inc[i-1] = tmp;
			dec[i-1] = 0;
		} else { // dec
			inc[i-1] = 0;
			dec[i-1] = -1*tmp;
		}
		mmi[i] = (inc[i-1] + mmi[i-1]*(13.0))/14.0;
		mmd[i] = (dec[i-1] + mmd[i-1]*(13.0))/14.0;
		ema14[i] = (val[i] - ema14[i-1])*w14 + ema14[i-1];
		if (i <= 8)
			ema9[i] = (val[i] - ema9[i-1])*w9 + ema9[i-1];
	}

	// EMA 28 for MACD
	ticks = getticks(m, "oneMin", 29);
	ema28[0] = ticks[28]->close;
	for (i=1; i <= 27; i++)
		ema28[i] = (ticks[28-i-1]->close - ema28[i-1])*w28 + ema28[i-1];
	free_ticks(ticks);

	i = 0;
	// poll every minutes
	while (i < 15) {
		begining = time(NULL);
		// this loop polls ~1/s api for tick
		while (difftime(time(NULL), begining) < 60) {
			tmptick = getticker(m);
			if (tmptick) {
				previous = (i + 13) % 14; // 13 when i = 0
				tmp = tmptick->last - val[previous];
				if (tmp >= 0) { //inc
					inc[previous] = tmp;
					dec[previous] = 0;
				} else { // dec
					inc[previous] = 0;
					dec[previous] = -1*tmp;
				}
				mmi[i] = (inc[previous] + mmi[previous]*(13.0))/14.0;
				mmd[i] = (dec[previous] + mmd[previous]*(13.0))/14.0;
				free(tmptick);
			}
			tmprsi =  100 * (1.0-(1.0/(1.0+mmi[i]/mmd[i])));
//			printf(" %f \n", tmprsi);
			if (tmprsi  >= 70 && buy) {
				double sellminusfee = (tmptick->last * buy->realqty) * ( 1 - 0.25/100);
				double estimatedgain = sellminusfee - buy->btcpaid;
				if (estimatedgain > 0) {
					printf("SELL %s at %.8f, quantity: %.8f, Gain: %.8f\n", m->marketname, tmptick->last, buy->realqty, estimatedgain);
					sell = new_trade(m, LIMIT, 1, tmptick->last, IMMEDIATE_OR_CANCEL, NONE, 0, SELL);
					processed_order(bbot->bi->connector, (char*)testuuidstr, estimatedgain);
					testuuid++;
					free(buy); buy = NULL;
					free(sell); sell = NULL;
				} else {
					if (previousloss != estimatedgain) {
						printf("Warning, RSI(tmp) of %s over 70 but no opportunity found (loss: %.8f)\n", m->marketname, estimatedgain);
						previousloss = estimatedgain;
					}
				}
			}
			sleep(1);
		}
		t = getticks(m, "oneMin", 1);
		if (t && t[0]) {
			val[i] = t[0]->close;
			previous = (i + 13) % 14; // 13 when i = 0
			tmp = val[i] - val[previous];
			if (tmp >= 0) { //inc
				inc[previous] = tmp;
				dec[previous] = 0;
			} else { // dec
				inc[previous] = 0;
				dec[previous] = -1*tmp;
			}
			mmi[i] = (inc[previous] + mmi[previous]*(13.0))/14.0;
			mmd[i] = (dec[previous] + mmd[previous]*(13.0))/14.0;
			ema14[i] = (val[i] - ema14[previous])*w14 + ema14[previous];
			if (j == 0)
				ema28[j] = (val[i] - ema28[27])*w28 + ema28[27];
			if (j >= 1)
				ema28[j] = (val[i] - ema28[j-1])*w28 + ema28[j-1];
			if (k == 0)
				ema9[k] = (val[i] - ema9[8])*w9 + ema9[8];
			if (k >= 1)
				ema9[k] = (val[i] - ema9[k-1])*w9 + ema9[k-1];
			pthread_mutex_lock(&(m->indicators_lock));
			m->rsi = 100 * (1-(1.0/(1+mmi[i]/mmd[i])));
			m->brsi = 100 * sum(inc, 14)/(sum(inc,14)+sum(dec,14));
			m->macd = ema14[i] - ema28[j];
			pthread_mutex_unlock(&(m->indicators_lock));
			printf("Market: %s, Wilder RSI: %.8f, Bechu RSI: %.8f, MACD: %.8f\n",
			       m->marketname, m->rsi, m->brsi, m->macd);
			// If rsi < 30 and we did not buy yet
			// rsi != 0 in case api replied crap at init
			if (m->rsi <= 30 && m->rsi != 0 && !buy) {
				last = getticker(m);
				if (last) {
					// btc available divided by the number of active bot markets
					btcqty = quantity(bbot) / bbot->active_markets;
					// qty of coin to be baught
					qty = btcqty / last->last;
					// order information
					printf("BUY %s at %.8f, quantity: %.8f (BTC: %.8f), fees: %.8f\n", m->marketname, last->last, qty, btcqty, (0.25/100) * qty * last->last);
					/* this instanciate a trade struct but it does not buy for real (API V2 not implemented) */
					/* but we can use trade struct fields */
					buy = new_trade(m, LIMIT, qty, t[0]->close, IMMEDIATE_OR_CANCEL, NONE, 0, BUY);
					buy->btcpaid = btcqty;
					//estimated fee (0.25%)
					buy->fee = (0.25/100) * qty * last->last;
					buy->realqty = qty - buy->fee;
					// trick for test (no dup uuid between threads)
					testuuid += last->last;
					sprintf(testuuidstr, "%.8f", testuuid);
					//uuid = buylimit(bi, m, qty, last->last);
					/* fixme : add getorder and process reply then update buy struct fields or replace buy struct by order struct? */
					insert_order(bbot->bi->connector, testuuidstr, m->marketname, buy->realqty, last->last, buy->btcpaid);
					free(last); last = NULL;
				}
			}
			// this sell is not probable (we sell mostly in first loop when RSI is refreshed within a minute)
			if (m->rsi >= 70 && buy) {
				last = getticker(m);
				double sellminusfee = (last->last * buy->realqty) * ( 1 - 0.25/100);
				double estimatedgain = sellminusfee - buy->btcpaid;
				if (estimatedgain > 0) {
					printf("SELL %s at %.8f, quantity: %.8f, Gain: %.8f\n", m->marketname, last->last, buy->realqty, estimatedgain);
					sell = new_trade(m, LIMIT, 1, last->last, IMMEDIATE_OR_CANCEL, NONE, 0, SELL);
					processed_order(bbot->bi->connector, (char*)testuuidstr, estimatedgain);
					testuuid++;
					free(buy); buy = NULL;
					free(sell); sell = NULL;
				} else {
					printf("Warning, RSI of %s over 70 but no opportunity found (loss: %.8f)\n", m->marketname, estimatedgain);
				}
			}
			free_ticks(t);
		} else {
			printf("gettick failed (skipping)\n");
			i--; //i-- so averages are not corrupted (not too much)
		}
		i++; j++; k++;
		if (i == 14) // for RSI
			i = 0;
		if (j == 28) // for EMA28 (macd)
			j = 0;
		if (k == 9) // for EMA 9
			k = 0;
		t = NULL;
	}
	return NULL;
}

/*
 * FREE functions
 */
void free_markets(struct market **markets) {
	struct market **tmp = markets;

        while (tmp && *tmp) {
		free_market(*tmp);
		tmp++;
	}
	free(markets);
}

void free_market(struct market *m) {
	if (m) {
		if (m->marketname)
			free(m->marketname);
		if (m->mh)
			free_market_history(m->mh);
		if (m->ms)
			free_market_summary(m->ms);
		if (m->ob)
			free_order_book(m->ob);
		free(m);
	}
}

void free_market_history(struct market_history **mh) {
	struct market_history **tmp = mh;

	while (tmp && *tmp) {
		free((*tmp)->timestamp);
		free((*tmp)->ctm);
		free((*tmp)->filltype);
		free((*tmp)->ordertype);
		free(*tmp);
		tmp++;
	}
	if (mh)
		free(mh);
}

void free_market_summary(struct market_summary *ms) {
	if (ms) {
		if (ms->timestamp)
			free(ms->timestamp);
		if (ms->ctm)
			free(ms->ctm);
		free(ms);
	}
}

void free_currencies(struct currency **currencies) {
	struct currency **tmp = currencies;

	while (tmp && *tmp) {
		free_currency(*tmp);
		tmp++;
	}
	if (currencies)
		free(currencies);
}

void free_currency(struct currency *c) {

	if (c) {
		free(c->coin);
		if (c->currencylong)
			free(c->currencylong);
		if (c->cointype)
			free(c->cointype);
		if (c->baseaddress)
			free(c->baseaddress);
		free(c);
	}
}

void free_order_book(struct orderbook *ob) {
	struct order **tmp;

	if (ob) {
		if (ob->buy) {
			tmp = ob->buy;
			while (tmp && *tmp) {
				free(*tmp);
				tmp++;
			}
		}
		if (ob->sell) {
			tmp = ob->sell;
			while (tmp && *tmp) {
				free(*tmp);
				tmp++;
			}
		}
		free(ob);
	}
}

void free_ticks(struct tick **t) {
	struct tick **tmp = t;

	while (tmp && *tmp) {
		free((*tmp)->timestamp);
		free(*tmp);
		tmp++;
	}
	free(t);
}

/*
 * Print Functions
 */
void printmarkethistory(struct market *m){
	struct market_history **tmp;

	if (m && m->mh) {
		tmp = m->mh;
		while (*tmp) {
			printf("Id:\t\t%d\n", (*tmp)->id);
			printf("TimeStamp:\t%s\n", (*tmp)->timestamp);
			printf("Quantity:\t%.8f\n", (*tmp)->quantity);
			printf("Price:\t\t%.8f\n", (*tmp)->price);
			printf("Total:\t\t%.8f\n", (*tmp)->total);
			printf("FillType:\t%s\n", (*tmp)->filltype);
			printf("OrderType:\t%s\n\n", (*tmp)->ordertype);
			tmp++;
		}
	}
}

void printorderbook(struct market *m) {
	struct order **tmp;

	if (m->ob) {
		tmp = m->ob->buy;
		if (tmp) {
			printf("Buy Orderbook\n");
			while (*tmp) {
				printf("Quantity: %.8f, rate: %.8f\n", (*tmp)->quantity, (*tmp)->rate);
				tmp++;
			}
		}
		tmp = m->ob->sell;
		if (tmp) {
			printf("Sell Orderbook\n");
			while (tmp &&*tmp) {
				printf("Quantity: %.8f, rate: %.8f\n", (*tmp)->quantity, (*tmp)->rate);
				tmp++;
			}
		}
	}
}

void printmarkets(struct market **markets) {
	struct market **tmp = markets;

	while (tmp && *tmp) {
		if ((*tmp)->marketcurrency)
			printf("MarketCurrency:\t\t %s\n", (*tmp)->marketcurrency);
		if ((*tmp)->basecurrency)
			printf("BaseCurrency:\t\t %s\n", (*tmp)->basecurrency);
		if ((*tmp)->marketcurrencylong)
			printf("MarketCurrencyLong:\t %s\n", (*tmp)->marketcurrencylong);
		if ((*tmp)->basecurrencylong)
			printf("BaseCurrencyLong:\t %s\n", (*tmp)->basecurrencylong);
		printf("MinTradeSize:\t\t %.8f\n", (*tmp)->mintradesize);
		if ((*tmp)->marketname)
			printf("MarketName:\t\t %s\n", (*tmp)->marketname);
		printf("IsActive:\t\t %d\n\n", (*tmp)->isactive);
		tmp++;
	}
}


void printmarketsummaries(struct market **m) {
	struct market **tmp = m;

	while (tmp && *tmp) {
		printf("MarketName:\t%s\n", (*tmp)->marketname);
		printmarketsummary(*tmp);
		printf("\n");
		tmp++;
	}
}

void printmarketsummary(struct market *m) {
	if (m && m->ms) {
		printf("High:\t\t%.8f\n", m->ms->high);
		printf("Low:\t\t%.8f\n", m->ms->low);
		printf("Volume:\t\t%.8f\n", m->ms->volume);
		printf("Last:\t\t%.8f\n", m->ms->last);
		printf("BaseVolume:\t%.8f\n", m->ms->basevolume);
		printf("Timestamp:\t%s\n", m->ms->timestamp);
		printf("Bid:\t\t%.8f\n", m->ms->bid);
		printf("Ask:\t\t%.8f\n", m->ms->ask);
		printf("OpenBuyOrders:\t%d\n", m->ms->openb);
		printf("OpenSellOrders:\t%d\n", m->ms->opens);
		printf("PrevDays:\t%.8f\n", m->ms->prevday);
	}
}

void printcurrencies(struct currency **currencies) {
	struct currency **tmp = currencies;

	while (*tmp) {
		printf("Coin:\t\t%s\n", (*tmp)->coin);
		printf("Name:\t\t%s\n", (*tmp)->currencylong);
		printf("Confirmation:\t%d\n", (*tmp)->minconfirmation);
		printf("TxFee:\t\t%.5f\n", (*tmp)->txfee);
		printf("Type:\t\t%s\n", (*tmp)->cointype);
		printf("BaseAddress:\t%s\n", (*tmp)->baseaddress);
		printf("\n");
		tmp++;
	}
}

void printtopN(struct market **markets, int n) {
	struct market **tmp = markets;
	int i = 0;

	for (i=0; i < n && *tmp; i++) {
		printf("Market:\t%s Volume:%.8f\n", (*tmp)->marketname, (*tmp)->basevolume);
		tmp++;
	}
}

void printticker(struct ticker *t) {
	if (t) {
		printf("Bid:\t%.8f\n", t->bid);
		printf("Ask:\t%.8f\n", t->ask);
		printf("Last:\t%.8f\n", t->last);
	}
}

void printticks(struct tick **ticks) {
	struct tick **tmp =  ticks;

	while (tmp && *tmp) {
		printtick(*tmp);
		tmp++;
	}
}

void printtick(struct tick *t) {
	if (t) {
		printf("Open:\t\t%.8f\n", t->open);
		printf("High:\t\t%.8f\n", t->high);
		printf("Low:\t\t%.8f\n", t->low);
		printf("Close:\t\t%.8f\n", t->close);
		printf("Volume:\t\t%.8f\n", t->volume);
		printf("BTC value:\t%.8f\n", t->btcval);
		printf("Timestamp:\t%s\n\n", t->timestamp);
	}
}
