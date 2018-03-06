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

int bot(struct bittrex_info *bi) {
	struct market **worthm;
	struct tick **ticks;
	pthread_t ind[MAX_ACTIVE_MARKETS], abort;
	int i, nbm = 0;
	double diffh, diffd;
	char c1, c2;

	worthm = malloc((MAX_ACTIVE_MARKETS+1) * sizeof(struct market*));
	printf("checking market performance of last day and hour (markets selection)\n");
	for (i=0; i<MAX_ACTIVE_MARKETS; i++) {
		ticks = getticks(bi->markets[i], "Hour", 24);
		if (!ticks)
			ticks = getticks(bi->markets[i], "Hour", 24);
 		diffh = (ticks[0]->close/ticks[1]->close - 1)*100;
		c1 = diffh >= 0 ? '+' : '\0';
		diffd = (ticks[0]->close/ticks[23]->close - 1)*100;
		c2 = diffd >= 0 ? '+' : '\0';
		printf("Market: %s, last hour: %c%.2f%%, last day: %c%.2f%%\n", bi->markets[i]->marketname, c1, diffh, c2,   diffd);
		if (diffh > 0 && diffd > 0 && nbm < MAX_ACTIVE_MARKETS) {
			worthm[nbm] = bi->markets[i];
			nbm++;
		}
		free(ticks);
	}
	worthm[nbm] = NULL;
	if (worthm[0] == NULL) {
		worthm[0] = bi->markets[0];
		worthm[1] = NULL;
		nbm = 1;
	}

	printf("Selected Markets: ");
	for (i=0; i < MAX_ACTIVE_MARKETS && worthm[i]; i++) {
		printf("%s ", worthm[i]->marketname);
	}
	printf("\n");

	for (i=0; i < nbm; i++) {
		pthread_create(&(ind[i]), NULL, indicators, worthm[i]);
	}

	for (i=0; i < nbm; i++) {
		pthread_join(ind[i], NULL);
	}
	return 0;
}
