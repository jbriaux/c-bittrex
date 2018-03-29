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
	m->macdsignal = 0;
	m->macdhisto = 0;

	pthread_mutex_init(&(m->indicators_lock), NULL);

	return m;
}

int getmarkets(struct bittrex_info *bi) {
	int i = 0;
	json_t *result, *raw, *market_name, *root, *tmp;
	struct market *m = NULL;


	root = api_call(bi, GETMARKETS, GETMARKETS);
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

struct ticker *getticker(struct bittrex_info *bi, struct market *m) {
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

	root = api_call(bi, url, GETTICKER);
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
struct tick **getticks(struct bittrex_info *bi,
		       struct market *m,
		       char *interval,
		       int nbtick,
		       int sort) {
	json_t *root, *result, *raw;
	char *url;
	int size,i;
	struct tick **ticks, *tick;

	if (!m || !m->marketname || (sort != ASCENDING && sort != DESCENDING)) {
		fprintf(stderr, "getticks: invalid parameter.\n");
		return NULL;
	}

	url = malloc((strlen(GETTICKS)+strlen(m->marketname)+
		      strlen(interval)+strlen("&tickInterval=")+1)*sizeof(char));
	url[0]='\0';
	url = strcat(url, GETTICKS);
	url = strcat(url, m->marketname);
	url = strcat(url, "&tickInterval=");
	url = strcat(url, interval);

	root = api_call(bi, url, GETTICKS);
	if (!root)
		return NULL;

	result = json_object_get(root,"result");

	size = json_array_size(result);
	m->lastnbticks = size;
	if (size == 0)
		return NULL;

	if (nbtick == 0)
		nbtick = size;

	ticks = malloc((nbtick+1) * sizeof(struct tick*));

	for (i=0; i < size && i < nbtick; i++) {
		if (sort == ASCENDING)
			raw = json_array_get(result, size-i-1);
		if (sort == DESCENDING)
			raw = json_array_get(result, i);
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

struct currency **getcurrencies(struct bittrex_info *bi) {
	struct currency *c, **currencies;
	json_t *root, *result, *tmp, *raw;
	int size=0, i;

	root = api_call(bi, GETCURRENCIES, GETCURRENCIES);
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

	root = api_call(bi, GETMARKETSUMMARIES, GETMARKETSUMMARIES);
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
int getmarkethistory(struct bittrex_info *bi, struct market *m) {
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

	root = api_call(bi, url, GETMARKETHISTORY);
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

int getmarketsummary(struct bittrex_info *bi, struct market *m) {
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

	root = api_call(bi, url, GETMARKETSUMMARY);
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
int getorderbook(struct bittrex_info *bi, struct market *m, char *type) {
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
	root = api_call(bi, url, GETORDERBOOK);
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
		m->ob->buy = getorders(tmp, size);

		tmp =  json_object_get(result, "sell");
		if (!json_is_array(tmp)) {
			fprintf(stderr, "getorderbook (both 2/2, sell): API returned not an array");
			return -1;
		}
		size = json_array_size(tmp);
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

struct tick **reverse_ticks(struct tick **ticks, int size) {
	struct tick **reverse;
	int i;

	reverse = malloc((size+1) * sizeof(struct tick*));
	for (i = 0; i < size; i++) {
		reverse[i] = malloc(sizeof(struct tick));
		reverse[i]->open = ticks[size-i-1]->open;
		reverse[i]->high = ticks[size-i-1]->high;
		reverse[i]->low = ticks[size-i-1]->low;
		reverse[i]->close = ticks[size-i-1]->close;
		reverse[i]->volume = ticks[size-i-1]->volume;
		reverse[i]->btcval = ticks[size-i-1]->btcval;
		if (ticks[size-i-1]->timestamp != NULL) {
			reverse[i]->timestamp = malloc(strlen(ticks[size-i-1]->timestamp)+1);
			reverse[i]->timestamp = strcpy(reverse[i]->timestamp, ticks[size-i-1]->timestamp);
		}
	}
	reverse[i] = NULL;
	free_ticks(ticks);
	return reverse;
}

static double sum(double *array, int index, int size) {
	int i = index, j;
	double tmp = 0.0;

	for (j=0; j < size; j++) {
		tmp += array[i];
		i++;
	}
	return tmp;
}

/*
 * RSI with normal averages
 */
double rsi_interval_period(struct bittrex_info *bi, struct market *m, char *interval, int period) {
	struct tick **ticks = NULL;
	double *gain, *loss;
	double *avg_gain, *avg_loss;
	double *rs, *rsi;
	double res;
	int i, k = 0;

	gain = malloc((2 * period - 1) * sizeof(double));
	loss = malloc((2 * period - 1) * sizeof(double));
	avg_gain = malloc(period * sizeof(double));
	avg_loss = malloc(period * sizeof(double));
	rs = malloc(period * sizeof(double));
	rsi = malloc(period * sizeof(double));

	/*
	 * Wait until API replies
	 */
	while (!ticks)
		ticks = getticks(bi, m, interval, period*2, DESCENDING);

	for (i=1; i < period * 2 ; i++) {
		gain[i-1] = (ticks[i]->close - ticks[i-1]->close > 0) ?
			ticks[i]->close - ticks[i-1]->close : 0;
		loss[i-1] = (ticks[i]->close - ticks[i-1]->close < 0) ?
			-1.0*(ticks[i]->close - ticks[i-1]->close) : 0;
	}

	for (k = 0; k < period; k++) {
		avg_gain[k] = sum(gain, k, period) / period;
		avg_loss[k] = sum(loss, k, period) / period;
		rs[k] = avg_gain[k] / avg_loss[k];
		rsi[k] = 100 - 100/(1.0+rs[k]);
	}
	res = rsi[k-1];

	free_ticks(ticks);
	free(gain);
	free(loss);
	free(avg_gain);
	free(avg_loss);
	free(rs);
	free(rsi);

	return res;
}

/*
 * RSI with modified moving average (tradingview values)
 */
double rsi_mma_interval_period(struct bittrex_info *bi, struct market *m, char *interval, int period) {
	struct tick **ticks = NULL;
	double *gain, *loss;
	double *avg_gain, *avg_loss;
	double *rs, *rsi;
	double res;
	double weight = 1.0/(period);
	int i, k = 0;

	/*
	 * Wait until API replies
	 */
	while (!ticks)
		ticks = getticks(bi, m, interval, 0, DESCENDING);

	if (m->lastnbticks == 0)
		return -1;

	gain = malloc((m->lastnbticks - 1) * sizeof(double));
	loss = malloc((m->lastnbticks - 1)* sizeof(double));
	avg_gain = malloc(m->lastnbticks * sizeof(double));
	avg_loss = malloc(m->lastnbticks * sizeof(double));
	rs = malloc(m->lastnbticks * sizeof(double));
	rsi = malloc(m->lastnbticks * sizeof(double));

	for (i = 1; i < m->lastnbticks; i++) {
		gain[i-1] = (ticks[i]->close - ticks[i-1]->close > 0) ?
			ticks[i]->close - ticks[i-1]->close : 0;
		loss[i-1] = (ticks[i]->close - ticks[i-1]->close < 0) ?
			-1.0*(ticks[i]->close - ticks[i-1]->close) : 0;
	}

	/* gain[0] and loss[0] are 0 and must be ignored */
	avg_gain[0] = sum(gain, 1, period) / period;
	avg_loss[0] = sum(loss, 1, period) / period;
	rs[0] = avg_gain[0] / avg_loss[0];
	rsi[0] = 100 - 100/(1.0+rs[0]);
	for (k = 1; k < (m->lastnbticks-period-1); k++) {
		avg_gain[k] =  gain[period+k] * weight + avg_gain[k-1]*(1-weight);
		avg_loss[k] =  loss[period+k] * weight + avg_loss[k-1]*(1-weight);
		rs[k] = avg_gain[k] / avg_loss[k];
		rsi[k] = 100 - 100/(1.0+rs[k]);
		printf("tick of %d, %.8f\t%.8f\t\n", k+period+1,ticks[k+period+1]->close, rsi[k]);
	}

	/* last candle returned, vary often (depends on current close) */
	res = rsi[k-1];
	free_ticks(ticks);
	free(gain);
	free(loss);
	free(avg_gain);
	free(avg_loss);
	free(rs);
	free(rsi);

	return res;
}

struct tick **getticks_rsi_mma_interval_period(struct bittrex_info *bi,
						struct market *m,
						char *interval,
						int period)
{
	struct tick **ticks = NULL;
	double *gain, *loss;
	double *avg_gain, *avg_loss;
	double *rs, *rsi;
	double weight = 1.0/(period);
	int i, k = 0;

	/*
	 * Wait until API replies
	 */
	while (!ticks)
		ticks = getticks(bi, m, interval, 0, DESCENDING);

	if (m->lastnbticks == 0)
		return NULL;

	gain = malloc((m->lastnbticks - 1) * sizeof(double));
	loss = malloc((m->lastnbticks - 1)* sizeof(double));
	avg_gain = malloc(m->lastnbticks * sizeof(double));
	avg_loss = malloc(m->lastnbticks * sizeof(double));
	rs = malloc(m->lastnbticks * sizeof(double));
	rsi = malloc(m->lastnbticks * sizeof(double));

	for (i = 1; i < m->lastnbticks; i++) {
		gain[i-1] = (ticks[i]->close - ticks[i-1]->close > 0) ?
			ticks[i]->close - ticks[i-1]->close : 0;
		loss[i-1] = (ticks[i]->close - ticks[i-1]->close < 0) ?
			-1.0*(ticks[i]->close - ticks[i-1]->close) : 0;
	}

	/* gain[0] and loss[0] are 0 and must be ignored */
	avg_gain[0] = sum(gain, 1, period) / period;
	avg_loss[0] = sum(loss, 1, period) / period;
	rs[0] = avg_gain[0] / avg_loss[0];
	rsi[0] = 100 - 100/(1.0+rs[0]);
	for (k = 1; k < (m->lastnbticks-period-1); k++) {
		avg_gain[k] =  gain[period+k] * weight + avg_gain[k-1]*(1-weight);
		avg_loss[k] =  loss[period+k] * weight + avg_loss[k-1]*(1-weight);
		rs[k] = avg_gain[k] / avg_loss[k];
		rsi[k] = 100 - 100/(1.0+rs[k]);
		ticks[k+period+1]->rsi_ema = rsi[k];
		/* printf("tick of %d, %.8f\t%.8f\t\n", k+period+1,ticks[k+period+1]->close, rsi[k]); */
	}

	free(gain);
	free(loss);
	free(avg_gain);
	free(avg_loss);
	free(rs);
	free(rsi);

	return ticks;
}

double *ema_interval_period(struct bittrex_info *bi, struct market *m, char *interval, int period) {
	struct tick **ticks = NULL;
	double weight = (2.0/(period+1));
	double *moving_average = NULL;
	int i, j = 0;

	if (!(moving_average = malloc(period * sizeof(double))))
		return NULL;
	moving_average[0] = 0;

	/*
	 * Wait until API replies
	 */
	while (!ticks)
		ticks = getticks(bi, m, interval, period*2, DESCENDING);

	/*
	 * init EMA(0)
	 */
	for (i = 0; i < period; i++) {
		moving_average[0] += ticks[i]->close;
		j++;
	}
	moving_average[0] /= period;

	for (i = 1; i < period; i++) {
		moving_average[i] = ticks[j]->close * weight + moving_average[i-1]*(1-weight);
		printf("%.8f\n", moving_average[i]);
		j++;
	}

	free_ticks(ticks);
	return moving_average;
}

double *mma_interval_period(struct bittrex_info *bi, struct market *m, char *interval, int period) {
	struct tick **ticks = NULL;
	double weight = (1.0/period);
	double *moving_average = NULL;
	int i, j = 0;

	if (!(moving_average = malloc(period * sizeof(double))))
		return NULL;
	moving_average[0] = 0;

	/*
	 * Wait until API replies
	 */
	while (!ticks)
		ticks = getticks(bi, m, interval, period*2, DESCENDING);

	/*
	 * init EMA(0)
	 */
	for (i = 0; i < period; i++) {
		moving_average[0] += ticks[i]->close;
		j++;
	}
	moving_average[0] /= period;

	for (i = 1; i < period; i++) {
		moving_average[i] = ticks[j]->close * weight + moving_average[i-1]*(1-weight);
		printf("%.8f\n", moving_average[i]);
		j++;
	}

	free_ticks(ticks);
	return moving_average;
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
		if (m->basecurrency)
			free(m->basecurrency);
		if (m->basecurrencylong)
			free(m->basecurrencylong);
		if (m->marketcurrency)
			free(m->marketcurrency);
		if (m->marketcurrencylong)
			free(m->marketcurrencylong);
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
			free(ob->buy);
		}
		if (ob->sell) {
			tmp = ob->sell;
			while (tmp && *tmp) {
				free(*tmp);
				tmp++;
			}
			free(ob->sell);
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
		if (t->timestamp)
			printf("Timestamp:\t%s\n\n", t->timestamp);
	}
}
