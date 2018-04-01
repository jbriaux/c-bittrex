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

#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "lib/jansson/src/jansson.h"
#include "lib/hmac/hmac_sha2.h"

#include "bot.h"
#include "market.h"
#include "bittrex.h"
#include "account.h"
#include "trade.h"

// for now BTC, add ETH & USDT
double quantity(struct bittrex_bot *bbot) {
	struct balance *b;
	struct currency *c;

	c = getcurrency(bbot->bi->currencies, "BTC");
	if (c) {
		b = getbalance(bbot->bi, c, bbot->bi->api);
		if (b)
			return b->available;
	}
	return 0;
}

int rankofmarket(struct bittrex_info *bi, struct market *m) {
	int i = 0, j = 1;

	/* refresh state */
	getmarketsummaries(bi);
	for (i=0; i < bi->nbmarkets; i++) {
		if (strncmp("BTC-", bi->markets[i]->marketname, 4) == 0) {
			if (strcmp(m->marketname, bi->markets[i]->marketname) == 0)
				return j;
			j++;
		}
	}
	return -1;
}

int bot(struct bittrex_info *bi) {
	struct bittrex_bot **bbot;
	struct market **worthm;
	/* struct tick **ticks; */
	pthread_t ind[MAX_ACTIVE_MARKETS]; /* abort; */
	int i, j = 0, nbm = 0;
	/* double diffh, diffd; */
	/* char c1, c2; */

	worthm = malloc((MAX_ACTIVE_MARKETS+1) * sizeof(struct market*));
	printf("checking market performance of last day and hour (markets selection), using BTCs markets only\n");
	for (i=0; i < bi->nbmarkets && j < MAX_ACTIVE_MARKETS; i++) {
		if (strncmp("BTC-", bi->markets[i]->marketname, 4) == 0) {
			/* ticks = getticks(bi->markets[i], "Hour", 24); */
			/* if (!ticks) */
			/* 	ticks = getticks(bi->markets[i], "Hour", 24); */
			/* diffh = (ticks[0]->close/ticks[1]->close - 1)*100; */
			/* c1 = diffh >= 0 ? '+' : '\0'; */
			/* diffd = (ticks[0]->close/ticks[23]->close - 1)*100; */
			/* c2 = diffd >= 0 ? '+' : '\0'; */
			/* printf("Market: %s, last hour: %c%.2f%%, last day: %c%.2f%%\n", bi->markets[i]->marketname, c1, diffh, c2,   diffd); */
			/* if (diffh > 0 && diffd > 0 && nbm < MAX_ACTIVE_MARKETS) { */
			/* 	worthm[nbm] = bi->markets[i]; */
			/* 	nbm++; */
			/* } */
			/* free(ticks); */
			worthm[nbm] = bi->markets[i];
			worthm[nbm]->bot_rank = nbm;
			nbm++;
			j++;
		}
	}

	worthm[nbm] = NULL;
	if (worthm[0] == NULL) {
		worthm[1] = NULL;
		nbm = 1;
	}

	bbot = malloc(nbm * sizeof(struct bittrex_bot*));

	printf("Selected Markets: ");
	for (i=0; i < MAX_ACTIVE_MARKETS && worthm[i]; i++) {
		bbot[i] = malloc(sizeof(struct bittrex_bot));
		bbot[i]->bi = bi;
		bbot[i]->market = worthm[i];
		bbot[i]->active_markets = nbm;
		printf("%s ", worthm[i]->marketname);
	}
	printf("\n");

	printf("BTC available for bot: %.8f\n", quantity(bbot[0]));
	for (i=0; i < nbm; i++) {
		pthread_create(&(ind[i]), NULL, runbot, bbot[i]);
	}

	for (i=0; i < nbm; i++) {
		pthread_join(ind[i], NULL);
	}
	return 0;
}

/*
 * Order insertion: state is pending
 * UUID: UUID of the order
 * type: buy or sell
 * qty: quantity baught or sold after fees (0.25% both case)
 * mname: market name
 * rate: rate the order was baught
 * btcpaid: BTC paid or
 *
 */
static int insert_order(MYSQL *connector, char *UUID, char *type, char *mname, double qty, double rate, double btc) {
	char *query = NULL;
	char qtystr[32], ratestr[32], btcstr[32];
	int query_status;

	sprintf(qtystr, "%.8f", qty);
	sprintf(ratestr, "%.8f", rate);
	sprintf(btcstr, "%.8f", btc);

	if (!(query = malloc(1024)))
		return -ENOMEM;

	query[0] = '\0';
	query = strcat(query, "INSERT INTO Orders (UUID,Market,Quantity,Rate,BotType,BotState,Btc) ");
	query = strcat(query, "VALUES (");
	query = strcat(query, "'"); query = strcat(query, UUID); query = strcat(query, "',");;
	query = strcat(query, "'"); query = strcat(query, mname); query = strcat(query, "',");
	query = strcat(query, "'"); query = strcat(query, qtystr); query = strcat(query, "',");
	query = strcat(query, "'"); query = strcat(query, ratestr); query = strcat(query, "',");
	query = strcat(query, "'"); query = strcat(query, type); query = strcat(query,"',");
	query = strcat(query, "'pending',");
	query = strcat(query, "'"); query = strcat(query, btcstr); query = strcat(query, "'");
	query = strcat(query, ");");

	query_status = mysql_query(connector, query);
	if (query_status != 0)
		fprintf(stderr, "MySQL query failed: '%s'", query);
	free(query);
	return query_status;
}

static int processed_sell_order(MYSQL *connector, char *UUID, double gain) {
	char *query = NULL;
	char gainstr[18];
	int query_status;

	sprintf(gainstr, "%.8f", gain);
	if (!(query = malloc(1024)))
		return -ENOMEM;

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
	if (query_status != 0)
		fprintf(stderr, "MySQL query failed: '%s'", query);
	free(query);
	return query_status;
}

static int processed_buy_order(MYSQL *connector, char *UUID) {
	char *query = NULL;
	int query_status;

	if (!(query = malloc(1024)))
		return -ENOMEM;

	query[0] = '\0';
	query = strcat(query, "UPDATE Orders SET BotState = 'processed' ");
	query = strcat(query, "WHERE UUID='");
	query = strcat(query, UUID);
	query = strcat(query, "';");
	printf("%s\n", query);

	query_status = mysql_query(connector, query);
	if (query_status != 0)
		fprintf(stderr, "MySQL query failed: '%s'", query);
	free(query);
	return query_status;
}


static int cancel_order(MYSQL *connector, char *UUID) {
	char *query = NULL;
	int query_status;

	if (!(query = malloc(1024)))
		return -ENOMEM;

	query[0] = '\0';
	query = strcat(query, "UPDATE Orders SET BotState = 'cancelled' WHERE UUID='");
	query = strcat(query, UUID);
	query = strcat(query, "';");

	query_status = mysql_query(connector, query);
	if (query_status != 0)
		fprintf(stderr, "MySQL query failed: '%s'", query);

	free(query);
	return query_status;
}

/*
 * For resuming the bot.
 * 1 thread = 1 order max opened so number of row if any is 1
 * there can't be two orders (buy & sell) in the same market
 */
static struct trade *unprocessed_order(MYSQL *connector, struct market *m, char *type) {
	char *query, *buffer, *uuid;
	int query_status;
	double qty, rate, btcpaid;
	MYSQL_RES *result;
	unsigned long *len;
	struct trade *t = NULL;

	query = malloc(strlen("SELECT * FROM Orders WHERE BotState = 'pending'")+
		       strlen("AND Market = '' AND BotType = '';") +
		       strlen(type) + strlen(m->marketname) + 1 );

	query[0] = '\0';
	query = strcat(query, "SELECT * FROM Orders WHERE BotState = 'pending' AND Market = '");
	query = strcat(query, m->marketname);
	query = strcat(query, "' AND BotType = '");
	query = strcat(query, type);
	query = strcat(query, "';");

	query_status = mysql_query(connector, query);
	if (query_status != 0) {
		fprintf(stderr, "MySQL query failed: '%s'", query);
		free(query);
		return NULL;
	}

	result = mysql_store_result(connector);

	if (result && mysql_num_rows(result) == 1) {
		MYSQL_ROW row;

		buffer = malloc(42);
		uuid = malloc(42);
		row = mysql_fetch_row(result);
		len = mysql_fetch_lengths(result);

		uuid[0] = '\0';
		strncat(uuid, row[1], len[1] +1);

		buffer[0] = '\0';
		strncat(buffer, row[3], len[3] + 1);
		sscanf(buffer, "%lf", &qty);

		buffer[0] = '\0';
		strncat(buffer, row[4], len[4] + 1);
		sscanf(buffer, "%lf", &rate);

		buffer[0] = '\0';
		strncat(buffer, row[7], len[7] + 1);
		sscanf(buffer, "%lf", &btcpaid);

		if (strcmp(type, "buy") == 0)
			t = new_trade(m, LIMIT, qty, rate, IMMEDIATE_OR_CANCEL, NONE, 0, BUY, uuid);
		if (strcmp(type, "sell") == 0)
			t = new_trade(m, LIMIT, qty, rate, IMMEDIATE_OR_CANCEL, NONE, 0, SELL, uuid);

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
 * or sell whatever RSI is and if gain >= 1%
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
 * EMA = exponential moving average
 */
void *runbot(void *b) {
	struct bittrex_bot *bbot = (struct bittrex_bot *)b;
	struct market *m = bbot->market;
	struct trade *buy=NULL, *sell=NULL;
	struct user_order *order = NULL, *sellorder = NULL;
	struct tick **hour_ticks = NULL, **minute_ticks = NULL;
	struct ticker *last = NULL, *tmptick = NULL;
	char *buyuuid = NULL, *selluuid = NULL;
	time_t begining, buytime;
	double btcqty = 0, qty = 0;
	int nbhourt = 0;
	double previousloss = 0;
	int market_rank = m->bot_rank;

	/*
	 * bot resuming
	 */
	pthread_mutex_lock(&(bbot->bi->bi_lock));
	buy = unprocessed_order(bbot->bi->connector, m, "buy");
	sell = unprocessed_order(bbot->bi->connector, m, "sell");
	pthread_mutex_unlock(&(bbot->bi->bi_lock));
	if (buy && sell) {
		fprintf(stderr,
			"Found buy and sell unprocessed for same market. Database corruption ?.");
		return NULL;
	}
	if (buy) {
		if ((order = getorder(bbot->bi, buy->uuid))) {
			if (order->isopen) {
				buyuuid = malloc(strlen(buy->uuid));
				buyuuid = strcpy(buyuuid, buy->uuid);
				buy->completed = 0;
			} else {
				free_user_order(order); order = NULL;
				free_trade(buy); buy = NULL;
			}
		} else {
			fprintf(stderr, "first getorder failed, can't resume");
			return NULL;
		}
	}
	if (sell) {
		if ((sellorder = getorder(bbot->bi, sell->uuid))) {
			if (sellorder->isopen) {
				selluuid = malloc(strlen(sell->uuid));
				selluuid = strcpy(selluuid, sell->uuid);
				sell->completed = 0;
			} else {
				free_user_order(order); order = NULL;
				free_trade(sell); sell = NULL;
			}
		} else {
			fprintf(stderr, "first getorder failed, can't resume");
			return NULL;
		}
	}

	printf("Started bot for market: %s\n", m->marketname);
	/*
	 * Poll every minutes
	 * Loop exit if market rank decreased after a successfull SELL order
	 */
	while (1) {
		begining = time(NULL);

		/*
		 * This loop polls ~5/s api for tick
		 * We sell in these condition:
		 * - RSI > 70 and gain > 0
		 * - RSI not > 70 but gain > 1%
		 */
		if (minute_ticks)
			free_ticks(minute_ticks);
		while (difftime(time(NULL), begining) < 60) {
			minute_ticks = getticks_rsi_mma_interval_period(bbot->bi, m, "oneMin", 14);
			if (tmptick) {
				free(tmptick);
				tmptick = NULL;
			}
			if ((tmptick = getticker(bbot->bi, m))) {
				if (buy && buy->completed) {
					double sellminusfee = (tmptick->last * buy->realqty) * ( 1 - 0.25/100);
					double estimatedgain = sellminusfee - buy->btcpaid;
					if ((estimatedgain > 0 && minute_ticks[m->lastnbticks-1]->rsi_ema >= 70) ||
					    (estimatedgain >= buy->btcpaid / 100)) {
						if (!sell) {
							sell = new_trade(m, LIMIT, 1, tmptick->last, IMMEDIATE_OR_CANCEL, NONE, 0, SELL, NULL);
							if (!(selluuid = selllimit(bbot->bi, m, buy->realqty, tmptick->last))) {
								printf("Something went wront when passing SELL order, uuid null\n");
								free_trade(sell); sell = NULL;
							} else {
								printf("SELL %s at %.8f, quantity: %.8f, Gain (if sold): %.8f\n",
								       m->marketname,
								       tmptick->last,
								       buy->realqty,
								       estimatedgain);
								while (!sellorder) {
									fprintf(stderr, "getorder: '%s' failed, retrying.\n",
										selluuid);
									sellorder = getorder(bbot->bi, selluuid);
								}
								pthread_mutex_lock(&(bbot->bi->bi_lock));
								insert_order(bbot->bi->connector, selluuid, "sell", m->marketname, buy->realqty, tmptick->last, estimatedgain);
								processed_buy_order(bbot->bi->connector, buyuuid);
								pthread_mutex_unlock(&(bbot->bi->bi_lock));
								free_trade(buy); buy = NULL;
							}
						}
					} else {
						if (previousloss != estimatedgain && minute_ticks[m->lastnbticks-1]->rsi_ema >= 70) {
							printf("Warning, RSI(tmp) of %s over 70 but no opportunity found (loss: %.8f)\n", m->marketname, estimatedgain);
							previousloss = estimatedgain;
						}
					}
				}
			}
			free_ticks(minute_ticks);
			if (!buy)
				sleep(5);
			else
				sleep(1);
		}

		if (hour_ticks)
			free_ticks(hour_ticks);
		if (tmptick) {
			free(tmptick);
			tmptick = NULL;
		}

		hour_ticks = getticks_rsi_mma_interval_period(bbot->bi, m, "Hour", 14);
		nbhourt = m->lastnbticks;
		minute_ticks = getticks_rsi_mma_interval_period(bbot->bi, m, "oneMin", 14);

		/*
		 * lock on the market as indicators will be shown (later) in a different thread.
		 */
		pthread_mutex_lock(&(m->indicators_lock));
		m->rsi = minute_ticks[m->lastnbticks-1]->rsi_ema;
		pthread_mutex_unlock(&(m->indicators_lock));

		while (!tmptick) {
			tmptick = getticker(bbot->bi, m);

		}

		fprintf(stderr,
			"Market: %s\tRSI(14,mn): %.8f\tRSI(14,h): %.8f\tlast: %.8f\n",
			m->marketname,
			m->rsi,
			hour_ticks[nbhourt-1]->rsi_ema,
			tmptick->last);

		free(tmptick);
		tmptick = NULL;

		/* refresh buy order state */
		if (buy && !buy->completed) {
			free_user_order(order);
			order = getorder(bbot->bi, buyuuid);
			if (order) {
				if (!order->isopen) {
					buy->fee = order->commission;
					buy->realqty = order->quantity;
					free_user_order(order);
					buy->completed = 1;
				} else if (difftime(time(NULL), buytime) >= 60) {
					/*
					 * one minute occured buy order not filled
					 * we let it if RSI is falling otherwise we cancel it
					 */

					if (minute_ticks[m->lastnbticks-1]->rsi_ema > 30 &&
					    minute_ticks[m->lastnbticks-1]->rsi_ema > minute_ticks[m->lastnbticks-2]->rsi_ema) {
						printf("Order not filled after %.2f seconds, RSI raising, canceling.\n", difftime(time(NULL), buytime));
						cancel(bbot->bi, buyuuid);
						pthread_mutex_lock(&(bbot->bi->bi_lock));
						cancel_order(bbot->bi->connector, buyuuid);
						pthread_mutex_unlock(&(bbot->bi->bi_lock));
						free_user_order(order);
						free(buyuuid); buyuuid = NULL;
						//free_trade(buy);
						buy = NULL;
					}
				}
			}
		}

		/*
		 * refresh sell order state
		 * we free buyuuid and sell + selluuid when sell order completes.
		 * Check if the state of the market changed (in volume)
		 * in case of change, exit and open a new thread on another market.
		 */
		if (sell && !sell->completed) {
			free_user_order(sellorder);
			sellorder = getorder(bbot->bi, selluuid);
			if (sellorder && !sellorder->isopen) {
				pthread_mutex_lock(&(bbot->bi->bi_lock));
				bbot->bi->trades_active--;
				pthread_mutex_unlock(&(bbot->bi->bi_lock));
				processed_sell_order(bbot->bi->connector, selluuid, sellorder->price);
				free_user_order(sellorder);
				sellorder = NULL;
				free_trade(sell); sell = NULL;
				free(selluuid); selluuid = NULL;
				free(buyuuid); buyuuid = NULL;
				if (rankofmarket(bbot->bi, m) < market_rank) {
					printf("Market(%s) lost rank, exiting\n", m->marketname);
					return NULL;
				} else {
					printf("Market(%s) rank increased! Good, continuing.\n", m->marketname);
				}
			}
		}

		/*
		 * If rsi < 30 and we did not buy yet, we buy
		 *
		 * rsi != 0 in case of init failure (need to confirm it is fixed)
		 * but should be removed
		 */
		if (m->rsi < 30 && m->rsi != 0 && !buy && !sell && hour_ticks[nbhourt-1]->rsi_ema <= 35) {
			last = getticker(bbot->bi, m);
			if (last) {
				/* btc available divided by the number of active bot markets */
				btcqty = quantity(bbot) / (bbot->active_markets - bbot->bi->trades_active);
				/* we use 99% of qty available */
				btcqty *= 0.99;
				/* qty of coin to be baught */
				qty = btcqty / last->last;
				/* order information */
				printf("BUY %s at %.8f, quantity: %.8f (BTC: %.8f), fees: %.8f\n", m->marketname, last->last, qty, btcqty, (0.25/100) * qty * last->last);
				/*
				 * This instanciate a trade struct but it does not buy for real (API V2 not implemented)
				 * but we can use trade struct fields
				 */
				buy = new_trade(m, LIMIT, qty, last->last, IMMEDIATE_OR_CANCEL, NONE, 0, BUY, NULL);
				buy->btcpaid = btcqty * 1.0025;
				buy->realqty = qty;
				if (!(buyuuid = buylimit(bbot->bi, m, qty, last->last))) {
					printf("Something went wront when passing BUY order, uuid null\n");
					free_trade(buy);
					buy = NULL;
				} else {
					buytime = time(NULL);
					pthread_mutex_lock(&(bbot->bi->bi_lock));
					bbot->bi->trades_active++;
					pthread_mutex_unlock(&(bbot->bi->bi_lock));
					/* we let some time to bittrex */
					sleep(3);
					order = getorder(bbot->bi, buyuuid);
					pthread_mutex_lock(&(bbot->bi->bi_lock));
					insert_order(bbot->bi->connector, buyuuid, "buy", m->marketname, buy->realqty, last->last, buy->btcpaid);
					pthread_mutex_unlock(&(bbot->bi->bi_lock));
					/* order already complete */
					if (order && !order->isopen) {
						buy->fee = order->commission;
						buy->realqty = order->quantity;
						free_user_order(order);
						buy->completed = 1;
					}
				}
				free(last); last = NULL;
			}
		}
		/*
		 * This sell is unlikely (we sell mostly in first loop when RSI is refreshed ~1/s)
		 */
		if (buy && buy->completed) {
			if ((last = getticker(bbot->bi, m))) {
				double sellminusfee = (last->last * buy->realqty) * ( 1 - 0.25/100);
				double estimatedgain = sellminusfee - buy->btcpaid;
				if ((estimatedgain > 0 && m->rsi >= 70) || (estimatedgain >= buy->btcpaid / 100)) {
					if (!sell) {
						sell = new_trade(m, LIMIT, 1, last->last, IMMEDIATE_OR_CANCEL, NONE, 0, SELL, NULL);
						if (!(selluuid = selllimit(bbot->bi, m, buy->realqty, last->last))) {
							printf("Something went wront when passing SELL order, uuid null\n");
							free_trade(sell); sell = NULL;
						} else {
							printf("SELL %s at %.8f, quantity: %.8f, Gain (if sold): %.8f\n", m->marketname, last->last, buy->realqty, estimatedgain);
							while (!sellorder) {
								fprintf(stderr, "getorder: '%s' failed, retrying.\n",
									selluuid);
								sellorder = getorder(bbot->bi, selluuid);
							}
							pthread_mutex_lock(&(bbot->bi->bi_lock));
							processed_buy_order(bbot->bi->connector, buyuuid);
							pthread_mutex_unlock(&(bbot->bi->bi_lock));
							free_trade(buy); buy = NULL;
						}
					}
				} else if (m->rsi >= 70) {
					printf("Warning, RSI of %s over 70 but no opportunity found (loss: %.8f)\n", m->marketname, estimatedgain);
				}
			}
		}
	}
	return NULL;
}

