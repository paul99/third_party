/*
 * Copyright (C) 2011 The Guava Authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.google.common.math;

import static com.google.common.math.MathBenchmarking.ARRAY_MASK;
import static com.google.common.math.MathBenchmarking.ARRAY_SIZE;
import static com.google.common.math.MathBenchmarking.RANDOM_SOURCE;
import static com.google.common.math.MathBenchmarking.randomExponent;
import static com.google.common.math.MathBenchmarking.randomNonNegativeBigInteger;
import static com.google.common.math.MathBenchmarking.randomPositiveBigInteger;

import com.google.caliper.Runner;
import com.google.caliper.SimpleBenchmark;
import com.google.common.math.LongMath;

/**
 * Benchmarks for the non-rounding methods of {@code LongMath}.
 *
 * @author Louis Wasserman
 */
public class LongMathBenchmark extends SimpleBenchmark {
  private static final int[] exponents = new int[ARRAY_SIZE];
  private static final int[] factorialArguments = new int[ARRAY_SIZE];
  private static final int[][] binomialArguments = new int[ARRAY_SIZE][2];
  private static final long[] positive = new long[ARRAY_SIZE];
  private static final long[] nonnegative = new long[ARRAY_SIZE];
  private static final long[] longs = new long[ARRAY_SIZE];

  @Override
  protected void setUp() {
    for (int i = 0; i < ARRAY_SIZE; i++) {
      exponents[i] = randomExponent();
      positive[i] = randomPositiveBigInteger(Long.SIZE - 2).longValue();
      nonnegative[i] = randomNonNegativeBigInteger(Long.SIZE - 2).longValue();
      longs[i] = RANDOM_SOURCE.nextLong();
      factorialArguments[i] = RANDOM_SOURCE.nextInt(30);
      binomialArguments[i][1] = RANDOM_SOURCE.nextInt(MathBenchmarking.BIGGEST_BINOMIALS.length);
      int k = binomialArguments[i][1];
      binomialArguments[i][0] =
          RANDOM_SOURCE.nextInt(MathBenchmarking.BIGGEST_BINOMIALS[k] - k) + k;
    }
  }

  public int timePow(int reps) {
    int tmp = 0;
    for (int i = 0; i < reps; i++) {
      int j = i & ARRAY_MASK;
      tmp += LongMath.pow(positive[j], exponents[j]);
    }
    return tmp;
  }

  public int timeMod(int reps) {
    int tmp = 0;
    for (int i = 0; i < reps; i++) {
      int j = i & ARRAY_MASK;
      tmp += LongMath.mod(longs[j], positive[j]);
    }
    return tmp;
  }

  public int timeGCD(int reps) {
    int tmp = 0;
    for (int i = 0; i < reps; i++) {
      int j = i & ARRAY_MASK;
      tmp += LongMath.mod(nonnegative[j], positive[j]);
    }
    return tmp;
  }

  public int timeFactorial(int reps) {
    int tmp = 0;
    for (int i = 0; i < reps; i++) {
      int j = i & ARRAY_MASK;
      tmp += LongMath.factorial(factorialArguments[j]);
    }
    return tmp;
  }

  public int timeBinomial(int reps) {
    int tmp = 0;
    for (int i = 0; i < reps; i++) {
      int j = i & ARRAY_MASK;
      tmp += LongMath.binomial(binomialArguments[j][0], binomialArguments[j][1]);
    }
    return tmp;
  }
  
  public static void main(String[] args) {
    Runner.main(LongMathBenchmark.class, args);
  }
}
