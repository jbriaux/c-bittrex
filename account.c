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
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>

#include "lib/hmac/hmac_sha2.h"
#include "account.h"
#include "bittrex.h"
#include "market.h"

/*
 * alloc struct api and fill key and secret
 */
struct api *new_api(char *apik, char *s){
	struct api *a;

	if (!apik || !s || strlen(apik) != 32 || strlen(s) != 32)
		return NULL;
	a = malloc(sizeof(struct api));
	a->key = apik;
	a->secret = s;
	return a;
}

/*
 * Check if API is valid (just expecting strings of 32 chars)
 */
int api_is_valid(struct api *api) {
	if (!api)
		return 0;
	if (!api->key || !api->secret)
		return 0;
	if (strlen(api->key) != 32 && strlen(api->secret) != 32)
		return 0;
	return 1;
}

struct balance *getbalance(struct currency *c, struct api *api) {
	struct balance *b;
	json_t *result, *tmp, *root;
	char *url, *nonce, *hmac;

	if (!c || !api || !api_is_valid(api)) {
		fprintf(stderr, "getbalance: invalid currency or invalid API key\n");
		return NULL;
	}

	nonce = getnonce();

	url = malloc(strlen(GETBALANCE) + strlen(api->key) +
		     strlen("&currency=") + strlen(c->coin) +
		     strlen("&nonce=") + strlen(nonce) + 1);
	url[0]='\0';
	url = strcat(url, GETBALANCE);
	url = strcat(url, api->key);
	url = strcat(url, "&currency=");
	url = strcat(url, c->coin);
	url = strcat(url, "&nonce=");
	url = strcat(url, nonce);
	hmac = hmacstr(api->secret, url);

	root = api_call_sec(url, hmac);
	if (!root) {
		fprintf(stderr, "getbalance: API call failed (%s)\n", url);
		free(url);
		return NULL;
	}

	result = json_object_get(root,"result");
	if (!result) {
		fprintf(stderr, "getbalance: invalid result field\n");
		return NULL;
	}

	b = malloc(sizeof(struct balance));
	b->currency = c;

	tmp = json_object_get(result, "Balance");
	b->balance = json_real_value(tmp);
	tmp = json_object_get(result, "Available");
	b->available = json_real_value(tmp);
	tmp = json_object_get(result, "Pending");
	b->pending = json_real_value(tmp);

	tmp = json_object_get(result, "Requested");
	b->requested = json_is_true(tmp);

	tmp = json_object_get(result, "CryptoAddress");
	b->cryptoaddress = json_string_get(b->cryptoaddress, tmp);

	json_decref(root);
	free(url);
	free(nonce);
	free(hmac);

	return b;
}

struct balance **getbalances(struct bittrex_info *bi, struct api *api) {
	struct balance **balances, *b;
	json_t *result, *tmp, *root, *raw;
	int size = 0, i = 0;
	char *url, *nonce, *hmac;

	if (!api || !api_is_valid(api)) {
		fprintf(stderr, "getbalances: no API key or invalid\n");
		return NULL;
	}

	nonce = getnonce();

	url = malloc(strlen(GETBALANCES) + strlen(api->key) +
		     strlen("&nonce=") + strlen(nonce) + 1);
	url[0]='\0';
	url = strcat(url, GETBALANCES);
	url = strcat(url, api->key);
	url = strcat(url, "&nonce=");
	url = strcat(url, nonce);
	hmac = hmacstr(api->secret, url);

	root = api_call_sec(url, hmac);
	if (!root) {
		fprintf(stderr, "getbalances: API call failed (%s)\n", url);
		free(url);
		return NULL;
	}

	result = json_object_get(root,"result");
	if (!result) {
		fprintf(stderr, "getbalances: invalid result field\n");
		return NULL;
	}
	if (!json_is_array(result)) {
		fprintf(stderr, "getbalances: invalid result field\n");
		return NULL;
	}

	if (!bi->currencies) {
		bi->currencies = getcurrencies();
	}
	size = json_array_size(result);
	balances = malloc((size+1)*sizeof(struct balance*));

	for (i=0; i < size; i++) {
		raw = json_array_get(result,i);
		b = malloc(sizeof(struct balance));

		tmp = json_object_get(raw, "Currency");
		if (tmp && json_string_value(tmp))
			b->currency = getcurrency(bi->currencies, (char*)json_string_value(tmp));

		tmp = json_object_get(raw, "Balance");
		b->balance = json_real_value(tmp);
		tmp = json_object_get(raw, "Available");
		b->available = json_real_value(tmp);
		tmp = json_object_get(raw, "Pending");
		b->pending = json_real_value(tmp);

		tmp = json_object_get(raw, "Requested");
		b->requested = json_is_true(tmp);

		tmp = json_object_get(raw, "CryptoAddress");
		b->cryptoaddress = json_string_get(b->cryptoaddress, tmp);

		balances[i] = b;
	}
	balances[i] = NULL;

	json_decref(root);
	free(url);
	free(nonce);
	free(hmac);

	return balances;
}

struct deposit **getdeposithistory(struct bittrex_info *bi, struct currency *c) {
	struct deposit **deposit, *d;
	json_t *result, *tmp, *root, *raw;
	int size = 0, i = 0;
	char *url, *nonce, *hmac;

	if (!bi->api || !api_is_valid(bi->api)) {
		fprintf(stderr, "getdeposithistory: no API key or invalid\n");
		return NULL;
	}

	nonce = getnonce();

	if (!c) {
		url = malloc(strlen(GETDEPOSITHISTORY) + strlen(bi->api->key) +
			     strlen("&nonce=") + strlen(nonce) + 1);
		url[0]='\0';
		url = strcat(url, GETDEPOSITHISTORY);
		url = strcat(url, bi->api->key);
		url = strcat(url, "&nonce=");
		url = strcat(url, nonce);
	} else {
		url = malloc(strlen(GETDEPOSITHISTORY) + strlen(bi->api->key) +
			     strlen("&currency=") + strlen(c->coin) +
			     strlen("&nonce=") + strlen(nonce) + 1);
		url[0]='\0';
		url = strcat(url, GETDEPOSITHISTORY);
		url = strcat(url, bi->api->key);
		url = strcat(url, "&currency=");
		url = strcat(url, c->coin);
		url = strcat(url, "&nonce=");
		url = strcat(url, nonce);
	}
	hmac = hmacstr(bi->api->secret, url);

	root = api_call_sec(url, hmac);
	if (!root) {
		fprintf(stderr, "getdeposithistory: API call failed (%s)\n", url);
		free(url);
		return NULL;
	}

	result = json_object_get(root,"result");
	if (!result) {
		fprintf(stderr, "getdeposithistory: invalid result field\n");
		return NULL;
	}
	if (!json_is_array(result)) {
		fprintf(stderr, "getdeposithistory: invalid result field\n");
		return NULL;
	}

	size = json_array_size(result);
	deposit = malloc((size+1)*sizeof(struct deposit*));

	for (i=0; i < size; i++) {
		raw = json_array_get(result,i);
		d = malloc(sizeof(struct deposit));

		tmp = json_object_get(raw, "Id");
		d->paymentuid = json_number_value(tmp);

		tmp = json_object_get(raw, "CryptoAddress");
		d->address = json_string_get(d->address, tmp);

		tmp = json_object_get(raw, "LastUpdated");
		d->timestamp = json_string_get(d->timestamp, tmp);

		tmp = json_object_get(raw, "Currency");
		if (tmp && json_string_value(tmp))
			d->currency = getcurrency(bi->currencies, (char*)json_string_value(tmp));

		tmp = json_object_get(raw, "Amount");
		d->amount = json_real_value(tmp);

		deposit[i] = d;
	}
	deposit[i] = NULL;

	free(url);
	free(nonce);
	free(hmac);
	json_decref(root);

	return deposit;
}

char *getdepositaddress(struct currency *c, struct api *api) {
	json_t *result, *root, *tmp;
	char *url, *nonce, *hmac, *res;

	if (!api_is_valid(api) || !c ) {
		fprintf(stderr, "getdepositaddress: bad parameter %s\n",
			api ? "currency": "api");
		return NULL;
	}

	nonce = getnonce();

	url = malloc(strlen(GETDEPOSITADDRESS) + strlen(api->key) +
		     strlen("&nonce=") + strlen(nonce) +
		     strlen("&currency=") + strlen(c->coin) +1);
	url[0]='\0';
	url = strcat(url, GETDEPOSITADDRESS);
	url = strcat(url, api->key);
	url = strcat(url, "&currency=");
	url = strcat(url, c->coin);
	url = strcat(url, "&nonce=");
	url = strcat(url, nonce);
	hmac = hmacstr(api->secret, url);

	root = api_call_sec(url, hmac);
	if (!root) {
		fprintf(stderr, "getdepositaddress: API call failed (%s)\n", url);
		free(url);
		return NULL;
	}

	result = json_object_get(root,"result");
	if (!result) {
		fprintf(stderr, "getdepositaddress: invalid result field\n");
		return NULL;
	}
	tmp = json_object_get(result, "Address");
	if (json_string_value(tmp) && strlen(json_string_value(tmp)) != 0)
		res = malloc(strlen(json_string_value(tmp)) + 1);
	else
		return NULL;
	res = strcpy(res, json_string_value(tmp));

	json_decref(root);
	free(url);
	free(nonce);
	free(hmac);

	return res;
}

int cancel(struct bittrex_info *bi, char *uuid) {
	struct user_order *o = NULL;
	json_t *root;
	char *url, *nonce, *hmac;

	if (!bi->api || !api_is_valid(bi->api)) {
		fprintf(stderr, "cancel: bad parameter API\n");
		return -1;
	}

	o = getorder(bi, uuid);
	if (!o) {
		fprintf(stderr, "Cancel order not possible, %s order not found\n", uuid);
	}

	nonce = getnonce();

	url = malloc(strlen(CANCELORDER) + strlen(bi->api->key) +
		     + strlen("&uuid=") + strlen(uuid) +
		     strlen("&nonce=") + strlen(nonce) + 1);
	url[0]='\0';
	url = strcat(url, CANCELORDER);
	url = strcat(url, bi->api->key);
	url = strcat(url, "&uuid=");
	url = strcat(url, uuid);
	url = strcat(url, "&nonce=");
	url = strcat(url, nonce);

	hmac = hmacstr(bi->api->secret, url);

	root = api_call_sec(url, hmac);
	if (!root) {
		fprintf(stderr, "cancel: API call failed (%s)\n", url);
		free(url);
		return -1;
	} else {
		printf("Order: %s canceled\n", uuid);
	}

	json_decref(root);
	free(url);
	free(nonce);
	free(hmac);

	return 0;
}

int withdraw(struct bittrex_info *bi, struct currency *c, double quantity, char *destaddress, char *paymentid) {
	json_t *result, *root, *tmp;
	char *url, *nonce, *hmac, *s = NULL;
	char buf[42];

	if (!api_is_valid(bi->api)) {
		fprintf(stderr, "withdraw: bad parameter API\n");
		return -1;
	}

	nonce = getnonce();

	// convert quantity into string
	sprintf(buf, "%.8f", quantity);

	if (!paymentid) {
		url = malloc(strlen(WITHDRAW) + strlen(bi->api->key) +
			     strlen("&currency=") + strlen(c->coin) +
			     strlen("&quantity=") + strlen(buf) +
			     strlen("&address=") + strlen(destaddress) +
			     strlen("&nonce=") + strlen(nonce) + 1);
	} else {
		url = malloc(strlen(WITHDRAW) + strlen(bi->api->key) +
			     strlen("&currency=") + strlen(c->coin) +
			     strlen("&quantity=") + strlen(buf) +
			     strlen("&address=") + strlen(destaddress) +
			     strlen("&paymentid=") + strlen(paymentid) +
			     strlen("&nonce=") + strlen(nonce) +1);
	}
	url[0] = '\0';
	url = strcat(url, WITHDRAW);
	url = strcat(url, bi->api->key);
	url = strcat(url, "&currency=");
	url = strcat(url, c->coin);
	url = strcat(url, "&quantity=");
	url = strcat(url, buf);
	url = strcat(url, "&address=");
	url = strcat(url, destaddress);
	if (paymentid) {
		url = strcat(url, "&paymentid=");
		url = strcat(url, paymentid);
	}
	url = strcat(url, "&nonce=");
	url = strcat(url, nonce);

	hmac = hmacstr(bi->api->secret, url);

	root = api_call_sec(url, hmac);
	if (!root) {
		fprintf(stderr, "withdraw: API call failed (%s)\n", url);
		free(nonce);
		free(url);
		return -1;
	}
	result = json_object_get(root, "result");
	tmp = json_object_get(result, "uuid");
	printf("Confirmation UUID: %s\n", (s=json_string_get(s,tmp)));

	json_decref(root);
	if (s) free(s);
	free(nonce);
	free(url);
	free(hmac);

	return 0;
}

/*
 * buylimit and selllimit api call
 */
static char *tradelimit(struct bittrex_info *bi, struct market *m, double quantity, double rate, char *type) {
	json_t *result, *root, *tmp;
	char *url, *nonce, *hmac, *s = NULL;
	char bufq[42], bufr[42];

	if (!api_is_valid(bi->api)) {
		fprintf(stderr, "%s: bad parameter API\n", type);
		return NULL;
	}

	nonce = getnonce();

	sprintf(bufq, "%.8f", quantity);
	sprintf(bufr, "%.8f", rate);

	url = malloc(strlen(type) + strlen(bi->api->key) +
		     strlen("&market=") + strlen(m->marketname) +
		     strlen("&quantity=") + strlen(bufq) +
		     strlen("&rate=") + strlen(bufr) +
		     strlen("&nonce=") + strlen(nonce) + 1);
	url[0] = '\0';
	url = strcat(url, type);
	url = strcat(url, bi->api->key);
	url = strcat(url, "&market=");
	url = strcat(url, m->marketname);
	url = strcat(url, "&quantity=");
	url = strcat(url, bufq);
	url = strcat(url, "&rate=");
	url = strcat(url, bufr);
	url = strcat(url, "&nonce=");
	url = strcat(url, nonce);

	hmac = hmacstr(bi->api->secret, url);

	root = api_call_sec(url, hmac);
	if (!root) {
		fprintf(stderr, "%s: API call failed (%s)\n", url, type);
		free(nonce);
		free(url);
		return NULL;
	}
	result = json_object_get(root, "result");
	tmp = json_object_get(result, "uuid");
	printf("UUID: %s\n", (s=json_string_get(s,tmp)));

	json_decref(root);

	free(nonce);
	free(url);
	free(hmac);

	return s;
}

char *buylimit(struct bittrex_info *bi, struct market *m, double quantity, double rate) {
	return tradelimit(bi, m, quantity, rate, BUYLIMIT);
}

char *selllimit(struct bittrex_info *bi, struct market *m, double quantity, double rate) {
	return tradelimit(bi, m, quantity, rate, SELLLIMIT);
}

/*
 * Used by getorder
 */
static void json_order_get(struct bittrex_info *bi, struct user_order *o, json_t *result){
	json_t *tmp = NULL;

	if (!o || !result)
		return;

	tmp = json_object_get(result, "OrderUuid");
	o->orderuuid = json_string_get(o->orderuuid, tmp);

	tmp = json_object_get(result, "Exchange");
	if (tmp && json_string_value(tmp)) {
		if (!bi->markets)
			getmarkets(bi);
		o->market = getmarket(bi->markets, (char*)json_string_value(tmp));
	}

	tmp = json_object_get(result, "Type");
	o->ordertype = json_string_get(o->ordertype, tmp);

	tmp = json_object_get(result, "Quantity");
	o->quantity = json_real_get(tmp);

	tmp = json_object_get(result, "QuantityRemaining");
	o->quantityremaining = json_real_get(tmp);

	tmp = json_object_get(result, "Limit");
	o->limit = json_real_get(tmp);

	tmp = json_object_get(result, "Reserved");
	o->reserved = json_real_get(tmp);

	tmp = json_object_get(result, "ReservedRemaining");
	o->reservedremaining = json_real_get(tmp);

	tmp = json_object_get(result, "CommissionReserved");
	o->commissionreserved = json_real_get(tmp);

	tmp = json_object_get(result, "CommissionReservedRemaining");
	o->commissionRR = json_real_get(tmp);

	tmp = json_object_get(result, "CommissionPaid");
	o->commission = json_real_get(tmp);

	tmp = json_object_get(result, "Price");
	o->price = json_real_get(tmp);

	tmp = json_object_get(result, "PricePerUnit");
	o->priceperunit = json_real_get(tmp);

	tmp = json_object_get(result, "Opened");
	o->timestamp = json_string_get(o->timestamp, tmp);

	tmp = json_object_get(result, "CancelInitiated");
	o->cancelinitiaded = json_real_get(tmp);

	tmp = json_object_get(result, "IsOpen");
	o->isopen = json_is_true(tmp);

	tmp = json_object_get(result, "IsConditional");
	o->isconditional = json_is_true(tmp);

	tmp = json_object_get(result, "Condition");
	o->condition = json_string_get(o->condition, tmp);

	tmp = json_object_get(result, "ConditionTarget");
	o->conditiontarget = json_string_get(o->conditiontarget, tmp);

	tmp = json_object_get(result, "ImmediateOrCancel");
	o->immediateorcancel = json_is_true(tmp);
}

struct user_order **getorderhistory(struct bittrex_info *bi, struct market *market) {
	struct user_order **orders, *o;
	json_t *result, *root, *tmp, *raw;
	char *url, *nonce, *hmac;
	int i = 0, size = 0;

	if (!api_is_valid(bi->api)) {
		fprintf(stderr, "getorderhistory: bad parameter API\n");
		return NULL;
	}

	nonce = getnonce();

	if (market) {
		url = malloc(strlen(GETORDERHISTORY) + strlen(bi->api->key) +
			     strlen(market->marketname) + strlen("&market=") +
			     strlen("&nonce=") + strlen(nonce) + 1);
		url[0]='\0';
		url = strcat(url, GETORDERHISTORY);
		url = strcat(url, bi->api->key);
		url = strcat(url, "&market=");
		url = strcat(url, market->marketname);
		url = strcat(url, "&nonce=");
		url = strcat(url, nonce);
	} else {
		url = malloc(strlen(GETORDERHISTORY) + strlen(bi->api->key) +
			     strlen("&nonce=") + strlen(nonce) + 1);
		url[0]='\0';
		url = strcat(url, GETORDERHISTORY);
		url = strcat(url, bi->api->key);
		url = strcat(url, "&nonce=");
		url = strcat(url, nonce);
	}
	hmac = hmacstr(bi->api->secret, url);

	root = api_call_sec(url, hmac);
	if (!root) {
		fprintf(stderr, "getorderhistory: API call failed (%s)\n", url);
		free(url);
		return NULL;
	}

	result = json_object_get(root,"result");
	if (!result || !json_is_array(result)) {
		fprintf(stderr, "getorderhistory: invalid result field\n");
		return NULL;
	}
	size = json_array_size(result);

	orders = malloc((size+1)*sizeof(struct user_order*));

	for (i=0; i < size; i++) {
		o = malloc(sizeof(struct user_order));
		raw = json_array_get(result,i);

		tmp = json_object_get(raw, "OrderUuid");
		o->orderuuid = json_string_get(o->orderuuid, tmp);

		tmp = json_object_get(raw, "Exchange");
		o->market = getmarket(bi->markets, (char*)json_string_value(tmp));

		tmp = json_object_get(raw, "TimeStamp");
		o->timestamp = json_string_get(o->timestamp, tmp);

		tmp = json_object_get(raw, "OrderType");
		o->ordertype = json_string_get(o->ordertype, tmp);

		tmp = json_object_get(raw, "Closed");
		o->dateclosed = json_string_get(o->dateclosed, tmp);

		tmp = json_object_get(raw, "Limit");
		o->limit= json_real_value(tmp);

		tmp = json_object_get(raw, "Quantity");
		o->quantity= json_real_value(tmp);

		tmp = json_object_get(raw, "QuantityRemaining");
		o->quantityremaining = json_real_value(tmp);

		tmp = json_object_get(raw, "Commission");
		o->commission = json_real_value(tmp);

		tmp = json_object_get(raw, "Price");
		o->price = json_real_value(tmp);

		tmp = json_object_get(raw, "PricePerUnit");
		o->priceperunit = json_real_value(tmp);

		tmp = json_object_get(raw, "IsConditional");
		o->isconditional = json_is_true(tmp);

		tmp = json_object_get(raw, "Condition");
		o->condition = json_string_get(o->condition, tmp);

		tmp = json_object_get(raw, "ConditionTarget");
		o->conditiontarget = json_string_get(o->conditiontarget, tmp);

		tmp = json_object_get(raw, "ImmediateOrCancel");
		o->immediateorcancel = json_is_true(tmp);

		orders[i] = o;
	}
	orders[i] = NULL;

	json_decref(root);
	free(url);
	free(nonce);
	free(hmac);

	return orders;
}

struct user_order **getopenorders(struct bittrex_info *bi, struct market *market) {
	struct user_order **orders, *o;
	json_t *result, *root, *tmp, *raw;
	char *url, *nonce, *hmac;
	int i = 0, size = 0;

	if (!api_is_valid(bi->api)) {
		fprintf(stderr, "getopenorders: bad parameter API\n");
		return NULL;
	}

	nonce = getnonce();

	if (market)
		url = malloc(strlen(GETOPENORDERS) + strlen(bi->api->key) +
			     strlen(market->marketname) + strlen("&market=") +
			     strlen("&nonce=") + strlen(nonce) + 1);
	else
		url = malloc(strlen(GETOPENORDERS) + strlen(bi->api->key) +
			     strlen("&nonce=") + strlen(nonce) + 1);
	url[0]='\0';
	url = strcat(url, GETOPENORDERS);
	url = strcat(url, bi->api->key);
	if (market) {
		url = strcat(url, "&market=");
		url = strcat(url, market->marketname);
	}
	url = strcat(url, "&nonce=");
	url = strcat(url, nonce);

	hmac = hmacstr(bi->api->secret, url);

	root = api_call_sec(url, hmac);
	if (!root) {
		fprintf(stderr, "getopenorders: API call failed (%s)\n", url);
		free(url);
		return NULL;
	}

	result = json_object_get(root,"result");
	if (!result || !json_is_array(result)) {
		fprintf(stderr, "getopenorders: invalid result field\n");
		return NULL;
	}
	size = json_array_size(result);

	orders = malloc((size+1)*sizeof(struct user_order*));

	for (i=0; i < size; i++) {
		o = malloc(sizeof(struct user_order));
		raw = json_array_get(result,i);

		tmp = json_object_get(raw, "OrderUuid");
		o->orderuuid = json_string_get(o->orderuuid, tmp);

		tmp = json_object_get(raw, "Exchange");
		o->market = getmarket(bi->markets, (char*)json_string_value(tmp));

		tmp = json_object_get(raw, "Opened");
		o->timestamp = json_string_get(o->timestamp, tmp);

		tmp = json_object_get(raw, "OrderType");
		o->ordertype = json_string_get(o->ordertype, tmp);

		tmp = json_object_get(raw, "Closed");
		o->dateclosed = json_string_get(o->dateclosed, tmp);

		tmp = json_object_get(raw, "Limit");
		o->limit= json_real_value(tmp);

		tmp = json_object_get(raw, "Quantity");
		o->quantity= json_real_value(tmp);

		tmp = json_object_get(raw, "QuantityRemaining");
		o->quantityremaining = json_real_value(tmp);

		tmp = json_object_get(raw, "CommissionPaid");
		o->commission = json_real_value(tmp);

		tmp = json_object_get(raw, "Price");
		o->price = json_real_value(tmp);

		tmp = json_object_get(raw, "PricePerUnit");
		o->priceperunit = json_real_value(tmp);

		tmp = json_object_get(raw, "IsConditional");
		o->isconditional = json_is_true(tmp);

		tmp = json_object_get(raw, "Condition");
		o->condition = json_string_get(o->condition, tmp);

		tmp = json_object_get(raw, "ConditionTarget");
		o->conditiontarget = json_string_get(o->conditiontarget, tmp);

		tmp = json_object_get(raw, "ImmediateOrCancel");
		o->immediateorcancel = json_is_true(tmp);

		orders[i] = o;
	}
	orders[i] = NULL;

	json_decref(root);
	free(url);
	free(nonce);
	free(hmac);

	return orders;
}

struct user_order *getorder(struct bittrex_info *bi, char *uuid){
	struct user_order *o;
	json_t *result, *root;
	char *url, *nonce, *hmac;

	if (!api_is_valid(bi->api) || !uuid ) {
		fprintf(stderr, "getorder: bad parameter %s\n",
			bi->api ? "UUID": "api");
		return NULL;
	}

	nonce = getnonce();

	url = malloc(strlen(GETORDER) + strlen(bi->api->key) +
		     strlen("&nonce=") + strlen(nonce) +
		     strlen("&uuid=") + strlen(uuid) +1);
	url[0]='\0';
	url = strcat(url, GETORDER);
	url = strcat(url, bi->api->key);
	url = strcat(url, "&uuid=");
	url = strcat(url, uuid);
	url = strcat(url, "&nonce=");
	url = strcat(url, nonce);
	hmac = hmacstr(bi->api->secret, url);

	root = api_call_sec(url, hmac);
	if (!root) {
		fprintf(stderr, "getorder: API call failed (%s)\n", url);
		free(url);
		return NULL;
	}

	result = json_object_get(root,"result");
	if (!result) {
		fprintf(stderr, "getorder: invalid result field\n");
		return NULL;
	}

	o = malloc(sizeof(struct user_order));
	json_order_get(bi, o, result);

	json_decref(root);
	free(url);
	free(nonce);
	free(hmac);

	return o;
}

void getwithdrawalhistory(struct bittrex_info *bi, struct currency *c) {
	json_t *result, *root, *raw, *tmp;
	char *url, *nonce, *hmac;
	int i = 0, size = 0;

	if (!api_is_valid(bi->api)) {
		fprintf(stderr, "getwithdrawalhistory: Invalid API");
		return;
	}

	nonce = getnonce();

	if (c) {
		url = malloc(strlen(GETWITHDRAWALHISTORY) + strlen(bi->api->key) +
			     strlen("&nonce=") + strlen(nonce) +
			     strlen("&currency=") + strlen(c->coin) + 1);
	} else {
		url = malloc(strlen(GETWITHDRAWALHISTORY) + strlen(bi->api->key) +
			     strlen("&nonce=") + strlen(nonce) + 1);
	}
	url[0]='\0';
	url = strcat(url, GETWITHDRAWALHISTORY);
	url = strcat(url, bi->api->key);
	if (c) {
		url = strcat(url, "&currency=");
		url = strcat(url, c->coin);
	}
	url = strcat(url, "&nonce=");
	url = strcat(url, nonce);

	hmac = hmacstr(bi->api->secret, url);

	root = api_call_sec(url, hmac);
	if (!root) {
		fprintf(stderr, "getwithdrawalhistory: API call failed (%s)\n", url);
		free(url);
		return;
	}

	result = json_object_get(root,"result");
	if (!result || !json_is_array(result)) {
		fprintf(stderr, "getwithdrawalhistory: invalid result field\n");
		return;
	}
	size = json_array_size(result);

	for (i=0; i < size; i++) {
		raw = json_array_get(result,i);

		tmp = json_object_get(raw, "PaymentUuid");
		if (tmp && json_string_value(tmp))
			printf("PaymentUuid:\t%s\n", json_string_value(tmp));

		tmp = json_object_get(raw, "Currency");
		if (tmp && json_string_value(tmp))
			printf("Currency:\t%s\n", json_string_value(tmp));

		tmp = json_object_get(raw, "Amount");
		printf("Amount: %.8f\n", json_real_get(tmp));

		tmp = json_object_get(raw, "Address");
		if (tmp && json_string_value(tmp))
			printf("Address:\t%s\n", json_string_value(tmp));

		tmp = json_object_get(raw, "Opened");
		if (tmp && json_string_value(tmp))
			printf("Opened:\t%s\n", json_string_value(tmp));

		tmp = json_object_get(raw, "Authorized");
		printf("Authorized:\t%d\n", json_is_true(tmp));

		tmp = json_object_get(raw, "PendingPayment");
		printf("PendingPayment:\t%d\n", json_is_true(tmp));

		tmp = json_object_get(raw, "TxCost");
		printf("TxCost: %.8f\n", json_real_get(tmp));

		tmp = json_object_get(raw, "TxId");
		if (tmp && json_string_value(tmp))
			printf("TxId:\t%s\n", json_string_value(tmp));

		tmp = json_object_get(raw, "Canceled");
		printf("Canceled:\t%d\n", json_is_true(tmp));

		tmp = json_object_get(raw, "InvalidAddress");
		printf("InvalidAddress:\t%d\n", json_is_true(tmp));
	}

	json_decref(root);
	free(url);
	free(nonce);
	free(hmac);
}

/*
 * FREE functions below
 */
void free_balances(struct balance **b) {
	struct balance **tmp = b;

	while (tmp && *tmp) {
		free_balance(*tmp);
		tmp++;
	}
	free(b);
}

void free_balance(struct balance *b) {
	//b->currency must not be freed here
	if (b) {
		if (b->cryptoaddress)
			free(b->cryptoaddress);
		b->currency = NULL;
		free(b);
	}
}

void free_api(struct api *a) {

	if (a) {
		/*
		 * key and secret are static for now
		 */
		//free(a->key);
		//free(a->secret);
		free(a);
	}
}

void free_deposit(struct deposit *deposit) {

	if (deposit) {
		free(deposit->address);
		free(deposit->timestamp);
		free(deposit->txid);
		deposit->currency = NULL;
		free(deposit);
	}
}

void free_user_order(struct user_order *o) {
	if (o) {
		free(o->orderuuid);
		free(o->timestamp);
		free(o->ordertype);
		if (o->dateclosed)
			free(o->dateclosed);
		if (o->condition)
			free(o->condition);
		if (o->conditiontarget)
			free(o->conditiontarget);
		o->market = NULL;
		free(o);
	}
}


/*
 * Print functions below
 */
void printorder(struct user_order *o) {
	if (o) {
		printf("UUID:\t\t\t\t%s\n", o->orderuuid);
		printf("Exchange:\t\t\t%s\n", o->market->marketname);
		printf("OrderType:\t\t\t%s\n", o->ordertype);
		printf("Quantity:\t\t\t%.8f\n", o->quantity);
		printf("QuantityRemaining:\t\t%.8f\n", o->quantityremaining);
		printf("Limit:\t\t\t\t%lf\n", o->limit);
		printf("Reserved:\t\t\t%.8f\n", o->reserved);
		printf("ReservedRemaining:\t\t%.8f\n", o->reservedremaining);
		printf("CommissionReserved:\t\t%.8f\n", o->commissionreserved);
		printf("CommissionReservedRemaining:\t%.8f\n", o->commissionRR);
		printf("CommissionPaid:\t\t\t%.8f\n", o->commission);
		printf("Price:\t\t\t\t%.8f\n", o->price);
		printf("PricePerUnit:\t\t\t%.8f\n", o->priceperunit);
		printf("Opened:\t\t\t\t%s\n", o->timestamp);
		printf("IsOpen:\t\t\t\t%s\n", o->isopen ? "true" : "false");
		printf("CancelInitiated:\t\t%s\n", o->cancelinitiaded ? "true" : "false");
		printf("ImmediateOrCancel:\t\t%s\n", o->immediateorcancel ? "true" : "false");
		printf("IsConditional:\t\t\t%s\n", o->isconditional ? "true" : "false");
		printf("Condition:\t\t\t%s\n", o->condition);
		printf("ConditionTarget:\t\t%s\n", o->conditiontarget);

	}
}

void printorders(struct user_order **o) {
	struct user_order **tmp = o;

	while (tmp && *tmp) {
		printorder(*tmp);
		printf("\n");
		tmp++;
	}
}

void printbalance(struct balance *b) {
	if (b) {
		if (b->currency)
			printf("Currency: %s\n", b->currency->coin);
		else
			printf("missing currency name\n");
		printf("Balance: %.8f\n", b->balance);
		printf("Available: %.8f\n", b->available);
		printf("Pending: %.8f\n", b->pending);
		printf("Requested: %d\n\n", b->requested);
	}
}

void printbalances(struct balance **b) {
	struct balance **tmp;

	if(b && *b) {
		tmp = b;
		while (*tmp) {
			printbalance(*tmp);
			tmp++;
		}
	}
}

void printdeposithistory(struct deposit **dep) {
	struct deposit **tmp = dep;

	while (tmp && *tmp) {
		printf("PaymentUid:\t%d\n", (*tmp)->paymentuid);
		printf("Currency:\t%s\n", (*tmp)->currency->coin);
		printf("Amount:\t\t%.8f\n", (*tmp)->amount);
		if ((*tmp)->address)
			printf("Address:\t%s\n", (*tmp)->address);
		if ((*tmp)->timestamp)
			printf("Opened:\t\t%s\n", (*tmp)->timestamp);
		printf("TxID:\t\t%s\n", (*tmp)->txid);
		printf("\n");
		tmp++;
	}
}
