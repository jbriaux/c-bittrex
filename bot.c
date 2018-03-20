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

#include "lib/jansson/src/jansson.h"
#include "lib/hmac/hmac_sha2.h"

#include "bot.h"
#include "market.h"
#include "bittrex.h"
#include "account.h"

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

int bot(struct bittrex_info *bi) {
	struct bittrex_bot **bbot;
	struct market **worthm;
	struct tick **ticks;
	pthread_t ind[MAX_ACTIVE_MARKETS], abort;
	int i, j = 0, nbm = 0;
	double diffh, diffd;
	char c1, c2;

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
		pthread_create(&(ind[i]), NULL, indicators, bbot[i]);
	}

	for (i=0; i < nbm; i++) {
		pthread_join(ind[i], NULL);
	}
	return 0;
}
