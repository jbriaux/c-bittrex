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

#include "bittrex.h"
#include "market.h"

#define BUY 1
#define SELL 2

typedef enum order_type order_type;
enum order_type { LIMIT, MARKET };

typedef enum time_type time_type;
enum time_type { IMMEDIATE_OR_CANCEL, GOOD_TIL_CANCELLED, FILL_OR_KILL};

typedef enum condition_type condition_type;
enum condition_type { NONE, GREATER_THAN, LESS_THAN };

struct trade {
	struct market *m;
	order_type type;
	double quantity; // quantity without fee
	double realqty; // real quantity baught (minus the fees, 0.25%)
	double rate;
	double fee;
	double btcpaid;
	time_type timeineffect;
	condition_type condition;
	int target;
	int buyorsell;
	int completed;
	char *uuid;
};

struct trade *new_trade(struct market *m,
			order_type type,
			double quantity,
			double rate,
			time_type timeineffect,
			condition_type condition,
			int target, int bos, char *uuid);

void free_trade(struct trade *t);
