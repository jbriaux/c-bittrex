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

#ifndef MARKET_H
#define MARKET_H

#include <time.h>
#include <pthread.h>
#include "lib/jansson/src/jansson.h"
#include "bittrex.h"
#include "bot.h"

/*
 * Market history
 * Used only in struct market
 */
struct market_history {
	int id;
	char *timestamp;
	struct tm *ctm; /* used for converting timestamp strings in time struct */
	double quantity;
	double total;
	double price;
	char *filltype;
	char *ordertype;
};

/*
 * Market summary
 * Used only in struct market
 */
struct market_summary {
	char *timestamp;
	struct tm *ctm; /* used for converting timestamp in time struct */
	double last, low, high, basevolume, volume, bid, ask,prevday;
	int openb, opens;
};

/*
 * Market
 */
struct market {
	char *marketname;
	char *marketcurrency;
	char *marketcurrencylong;
	char *basecurrency;
	char *basecurrencylong;
	int   isactive;
	double mintradesize;
	double high;
	double low;
	double basevolume; // duplicate of ms->basevolume (to speed up qsort)
	double volume;
	struct market_history **mh;
	struct market_summary *ms;
	struct orderbook *ob;
	double rsi; // Wilder RSI with mobile moving averages
	double brsi; // Bechu RSI
	double macd; // MACD 14 28 9 with exponential moving averages
	double macdsignal;
	double macdhisto;
	int bot_rank;

	/*
	 * When using bot mode, 1 thread / elected market is created
	 * rsi is updated once per mn
	 */
	pthread_mutex_t indicators_lock;
	/*
	 * keep track of ticks (vary from specified interval)
	 */
	int lastnbticks;
};

/*
 * Tick
 */
struct tick {
	double open;
	double high;
	double low;
	double close;
	double volume;
	double btcval;
	char *timestamp;
	double rsi;
	double rsi_mma;
	double rsi_ema;
};

/*
 * Ticker
 */
struct ticker {
	double bid;
	double ask;
	double last;
};

/*
 * Currency
 */
struct	currency {
	char *coin;
	char *currencylong;
	int minconfirmation;
	double txfee;
	int isactive;
	char *cointype;
	char *baseaddress;
};

/*
 * Orders of struct orderbook
 * related to market orderbook,
 * not related to user orders (trades).
 */
struct order {
	double quantity;
	double rate;
};

/*
 * Orderbook
 */
struct orderbook {
	struct order **buy;
	struct order **sell;
};

/* for sorting all markets by volume */
int compare_market_by_volume(const void *a, const void *b);

/*
 * get last ticker of given market(coin)
 */
struct ticker *getticker(struct bittrex_info *bi, struct market *m);

/*
 * get last tickers of given market and interval.
 * Interval can be oneMin fiveMin thirtyMin Hour
 * nbtick is mostly 14 (for RSI)
 */
struct tick **getticks(struct bittrex_info *bi, struct market *m, char *interval, int nbtick, int sort);

/*
 * fetch all available currencies
 */
struct currency **getcurrencies(struct bittrex_info *bi);

/*
 * return currency if currency name coin in array currencies is found
 */
struct currency *getcurrency(struct currency **currencies, char *coin);

/*
 * get market summary of last 24h
 */
int getmarketsummaries(struct bittrex_info *bi);

/*
 * get last 100 transaction of given market
 */
int getmarkethistory(struct bittrex_info *bi, struct market *m);

/*
 * get market summary of given market
 */
int getmarketsummary(struct bittrex_info *bi, struct market *m);

/*
 * get orderbook of given market
 */
int getorderbook(struct bittrex_info *bi, struct market *m, char *type);

/*
 * Get available markets
 */
int getmarkets(struct bittrex_info *bi);

/*
 * return market if marketname is found in array markets
 */
struct market *getmarket(struct market **markets, char *marketname);

/*
 * Just do allocation and set pointers fields to NULL
 */
struct market *new_market();

/*
 * RSI(period)
 * Interval: oneMin|fiveMin|thirtyMin|Hour
 */
double rsi_interval_period(struct bittrex_info *bi, struct market *m, char *interval, int period);
double rsi_mma_interval_period(struct bittrex_info *bi, struct market *m, char *interval, int period);

double *mma_interval_period(struct bittrex_info *bi, struct market *m, char *interval, int period);


/*
 * Exponential Moving Average (period)
 * Interval: oneMin|fiveMin|thirtyMin|Hour
 */
double *ema_interval_period(struct bittrex_info *bi, struct market *m, char *interval, int period);
struct tick **getticks_rsi_mma_interval_period(struct bittrex_info *bi,
					       struct market *m,
					       char *interval,
					       int period);


/*
 * Free functions
 */
void free_markets(struct market **markets);
void free_market(struct market *m);
void free_market_history(struct market_history **mh);
void free_market_summary(struct market_summary *ms);
void free_currencies(struct currency **currencies);
void free_currency(struct currency *c);
void free_order_book(struct orderbook *ob);
void free_ticks(struct tick **t);


/*
 * return 0 if marketname found in mtab
 */
int market_exists(struct market **mtab, char *marketname);

/*
 * Print functions
 */
void printticker(struct ticker *t);
void printcurrencies(struct currency **currencies);
void printmarkets(struct market **markets);
void printtopN(struct market **markets, int n);
void printmarkethistory(struct market *m);
void printmarketsummary(struct market *m);
void printmarketsummaries(struct market **m);
void printorderbook(struct market *m);
void printticks(struct tick **t);
void printtick(struct tick *t);

#endif
