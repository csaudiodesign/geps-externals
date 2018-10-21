/*
 * GePS - Gesture-based Performance System
 * external implementing GePS Feedback Network
 * 
 * Copyright (C) 2018  Cedric Spindler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * include the interface to Pd 
 */
#include "m_pd.h"

/**
 * class specific includes
 */
#include <string.h>
#include <math.h>

/**
 * define a new "class" 
 */
static t_class *fbnet_tilde_class;

typedef struct _dcblock_memory {
  t_sample x1;
  t_sample y1;
} t_dcblock_memory;

typedef struct _delay {
  t_sample *memory;
	long size, wrap;
	long reader, writer;

} t_delay;

typedef struct _lowpass {
  t_sample z1, z2;
  t_sample b0, b1, b2;
} t_lowpass;

/**
 * this is the dataspace of our new object
 * we don't need to store anything,
 * however the first (and only) entry in this struct
 * is mandatory and of type "t_object"
 */
typedef struct _fbnet_tilde {
  t_object  x_obj;
  float x_f;

  /* fbnet */
  float xgain;
  float igain;

  // node 1 (coupled with node 2)
  t_sample n1_x;
  t_dcblock_memory n1_dcblock_1;
  t_dcblock_memory n1_dcblock_2;
  t_delay n1_delay_1;
  t_lowpass n1_lowpass_1;

  // node 2 (coupled with node 1)
  t_sample n2_x;
  t_dcblock_memory n2_dcblock_1;
  t_dcblock_memory n2_dcblock_2;
  t_delay n2_delay_1;
  t_lowpass n2_lowpass_1;

  // node 3 (downstream of node 1)
  t_sample n3_x;
  t_dcblock_memory n3_dcblock_1;
  t_delay n3_delay_1;
  t_lowpass n3_lowpass_1;

  // node 4 (downstream of node 2)
  t_sample n4_x;
  t_dcblock_memory n4_dcblock_1;
  t_delay n4_delay_1;
  t_lowpass n4_lowpass_1;

  /* outlets */
  t_inlet *x_in2, *x_in3;
  t_outlet *x_out1;
} t_fbnet_tilde;

// Prototypes
extern t_sample ftanhf(t_sample x);
t_sample dcblock(t_dcblock_memory *dcblock_mem, t_sample in1);
void dcblock_reset(t_dcblock_memory *dcblock_mem);
t_int *fbnet_tilde_perform(t_int *w);
void fbnet_tilde_dsp(t_fbnet_tilde *x, t_signal **sp);
void fbnet_tilde_bang(t_fbnet_tilde *x);
void fbnet_tilde_test(t_fbnet_tilde *x, t_float argument);
static void fbnet_tilde_free(t_fbnet_tilde *x);
void *fbnet_tilde_new(void);
void fbnet_tilde_setup(void);

inline t_sample ftanhf(t_sample x)
{
  if(x > 4.8)
    return 1;
  else if(x <= -4.8)
    return -1;
  else {
    t_sample x2 = x * x;
    t_sample a = x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2)));
    t_sample b = 135135.0f + x2 * (62370.0f + x2 * (3150.0f + x2 * 28.0f));
    return a / b;
  }
}

/**
 * dcblock routine
 */ 
t_sample dcblock(t_dcblock_memory *dcblock_mem, t_sample in1)
{
  t_sample y = in1 - dcblock_mem->x1 + dcblock_mem->y1*((t_sample)0.9997);
  dcblock_mem->x1 = in1;
  dcblock_mem->y1 = y;
  return y;
}

/**
 * dcblock reset routine
 */ 
void dcblock_reset(t_dcblock_memory *dcblock_mem)
{
  dcblock_mem->x1 = 0;
  dcblock_mem->y1 = 0;
}

/**
 * clamp helper: minimum
 */
t_sample minimum(t_sample x, t_sample y)
{
  return (y < x ? y : x);
}

/**
 * clamp helper: maximum
 */ 
t_sample maximum(t_sample x, t_sample y)
{
  return (x < y ? y : x);
}

/**
 * clamp routine
 */
t_sample clamp(t_sample x, t_sample minVal, t_sample maxVal)
{
 	return minimum(maximum(x, minVal), maxVal);
}

/*
 * delay operations
 * requires delay struct with reader & writer
 * - read
 * - write
 * - step
 * - reset (zero delayline)
 */

t_sample delay_read(t_delay *d, t_sample t)
{
  // t for tap, supplied as signal input, therefore t_sample
  
  // clamp tap between 1 and size samples (maybe write a int version of clamp)
  t_sample c = (t_sample) clamp(t, (t_sample)(d->reader != d->writer), (t_sample)(d->size));

  t_sample r = (t_sample)(d->size + d->reader) - c;
  long r1 = (long)r;
  long r2 = r1 + 1;
  t_sample a = r - (t_sample)r1;
  t_sample x = d->memory[r1 & d->wrap];
  t_sample y = d->memory[r2 & d->wrap];
  return x + a * (y - x); // linear interpolation
}

void delay_write(t_delay *d, t_sample x)
{
  d->writer = d->reader;	// update write ptr
  d->memory[d->writer] = x;
}

void delay_step(t_delay *d)
{
  d->reader++;
  if (d->reader >= d->size) d->reader = 0;
}

void delay_reset(t_delay *d)
{
  // zero delay line
  memset(d->memory, 0, sizeof(t_sample) * d->size);
  d->reader = d->writer;
	d->wrap = d->size-1;
}

/**
 * 2nd order FIR lowpass filter
 * https://www.music.mcgill.ca/~gary/307/week2/filters.html
 */
t_sample lowpass_filter(t_lowpass *lpf, t_sample in)
{
  t_sample out = (lpf->b0 * in) + (lpf->b1 * lpf->z1) + (lpf->b2 * lpf->z2);
  
  // feedforward 
  lpf->z2 = lpf->z1;
  lpf->z1 = in;
  
  // feedback
  // lpf->z2 = lpf->z1;
  // lpf->z1 = out;

  return out;
}

void lowpass_reset(t_lowpass *lpf)
{
  lpf->b0 = 0.5;
  lpf->b1 = 0.25;
  lpf->b2 = 0.25;
  lpf->z1 = 0;
  lpf->z2 = 0;
}

/**
 * this is the core of the object
 * this perform-routine is called for each signal block
 * the name of this function is arbitrary and is registered to Pd in the 
 * fbnet_tilde_dsp() function, each time the DSP is turned on
 *
 * the argument to this function is just a pointer within an array
 * we have to know for ourselves how many elements in this array are
 * reserved for us (hint: we declare the number of used elements in the
 * fbnet_tilde_dsp() at registration
 *
 * since all elements are of type "t_int" we have to cast them to whatever
 * we think is apropriate; "apropriate" is how we registered this function
 * in fbnet_tilde_dsp()
 */
t_int *fbnet_tilde_perform(t_int *w)
{
  /* the first element (w[0]) is a pointer to dsp-perform itself */

  /* the second element is a pointer to the dataspace of this object */
  t_fbnet_tilde *x = (t_fbnet_tilde *)(w[1]);

  /* pointers to the t_sample arrays that hold the 2 input signals */
  t_sample *in1 = (t_sample *)(w[2]);
  t_sample *in2 = (t_sample *)(w[3]);
  t_sample *in3 = (t_sample *)(w[4]);

  /* pointers to the t_sample arrays that hold the 2 output signals */
  t_sample *out1 = (t_sample *)(w[5]);

  /* important: 
   * - in1 and out2 point to same memory
   * - in2 and out1 point to same memory
   * ins : 10184be70 10184b8a0
   * outs : 10184b8a0 10184be70
   */

  /* all signalblocks are of the same length */
  int n = (int)(w[6]);

  /* temporary storage for samples, to be able to get from in1 to out1 etc. */
  // t_sample l = 0;
  // t_sample r = 0;

  /* do your magic here */
  while(n--){
    t_sample input = *(in1++);
    t_sample mod1 = *(in2++);
    t_sample mod2 = *(in3++);

    /* 
     * start of routine
     * notation: node 1 multiply 1 -> n1_mul_1 
     */

    // pre out node 1
    t_sample n1_mul_1 = ftanhf(x->n1_x * x->igain);
    t_sample n1_mul_2 = ftanhf(x->n2_x * x->xgain); // crossfeed from n2
    t_sample n1_lowpass_1 = lowpass_filter(&x->n1_lowpass_1, n1_mul_1);
    t_sample n1_out = (PD_BIGORSMALL(n1_lowpass_1) ? 0 : n1_lowpass_1);

    // pre out node 2
    t_sample n2_mul_1 = ftanhf(x->n2_x * x->igain);
    t_sample n2_mul_2 = ftanhf(x->n1_x * x->xgain); // crossfeed from n1
    t_sample n2_lowpass_1 = lowpass_filter(&x->n2_lowpass_1, n2_mul_1);
    t_sample n2_out = (PD_BIGORSMALL(n2_lowpass_1) ? 0 : n2_lowpass_1);

    // -----

    // post out node 1
    t_sample n1_tap_1 = delay_read(&x->n1_delay_1, mod1);
    t_sample n1_dcblock_2 = dcblock(&x->n1_dcblock_2, n1_tap_1);
    x->n1_x = ftanhf(n1_dcblock_2);
    delay_write(&x->n1_delay_1, (input + n1_mul_2 + n1_lowpass_1) / 2.0); // input + crossfeed from n2
    delay_step(&x->n1_delay_1);

    // post out node 2
    t_sample n2_tap_1 = delay_read(&x->n2_delay_1, mod2);
    t_sample n2_dcblock_2 = dcblock(&x->n2_dcblock_2, n2_tap_1);
    x->n2_x = ftanhf(n2_dcblock_2);
    delay_write(&x->n2_delay_1, (n2_mul_2 + n2_lowpass_1) / 2.0);
    delay_step(&x->n2_delay_1);

    // -----

    // node 3, downstream delay with lowpass filter
    t_sample n3_lowpass_1 = lowpass_filter(&x->n3_lowpass_1, x->n3_x);
    t_sample n3_out = dcblock(&x->n3_dcblock_1, n3_lowpass_1);

    t_sample n3_tap_1 = delay_read(&x->n3_delay_1, (mod1 * 2.04));
    t_sample n3_mul_1 = 0.97 * n3_tap_1;
    t_sample n3_mix_1 = (n3_mul_1 + n3_lowpass_1) / 2.0;
    x->n3_x = (PD_BIGORSMALL(n3_mix_1) ? 0 : n3_mix_1);
    delay_write(&x->n3_delay_1, (n3_mix_1 + n1_out));
    delay_step(&x->n3_delay_1);

    // node 4, downstream delay with lowpass filter
    t_sample n4_lowpass_1 = lowpass_filter(&x->n4_lowpass_1, x->n4_x);
    t_sample n4_out = dcblock(&x->n4_dcblock_1, n4_lowpass_1);

    t_sample n4_tap_1 = delay_read(&x->n4_delay_1, (mod2 * 1.97));
    t_sample n4_mul_1 = 0.97 * n4_tap_1;
    t_sample n4_mix_1 = (n4_mul_1 + n4_lowpass_1) / 2.0;
    x->n4_x = (PD_BIGORSMALL(n4_mix_1) ? 0 : n4_mix_1);
    delay_write(&x->n4_delay_1, (n4_mix_1 + n2_out));
    delay_step(&x->n4_delay_1);

    // -----

    *out1++ = 0.5 * (n3_out + n4_out);
  }

  /* return a pointer to the dataspace for the next dsp-object */
  /* The perform-routine has to return a pointer to integer, 
   * that points directly behind the memory, where the object's 
   * pointers are stored. This means, that the return-argument 
   * equals the routine's argument w plus the number of used 
   * pointers (as defined in the second argument of dsp_add) 
   * plus one. */
  return (w+7);
}

/**
 * register a special perform-routine at the dsp-engine
 * this function gets called whenever the DSP is turned ON
 * the name of this function is registered in fbnet_tilde_setup()
 */
void fbnet_tilde_dsp(t_fbnet_tilde *x, t_signal **sp)
{
  /*
   * add fbnet_tilde_perform() to the DSP-tree.
   * the perform routine will expect 6 arguments, packed
   * into an t_int-array, which are:
   * - the objects data-space (x)
   * - 2 input signals
   * - 2 output signals
   * - the length of the signal vectors (all vectors are of the same length)
   */
  // post("ins : %lx %lx", sp[0]->s_vec, sp[1]->s_vec);
  // post("outs : %lx %lx", sp[2]->s_vec, sp[3]->s_vec);
  dsp_add(
    fbnet_tilde_perform,
    6,
    x,
    sp[0]->s_vec,
    sp[1]->s_vec,
    sp[2]->s_vec,
    sp[3]->s_vec,
    sp[0]->s_n
  );
}


/**
 * this method is called whenever a "bang" is sent to the object
 * the name of this function is arbitrary and is registered to Pd in the 
 * fbnet_tilde_setup() routine
 */
void fbnet_tilde_bang(t_fbnet_tilde *x)
{
  /*
  * post() is Pd's version of printf()
  * the string (which can be formatted like with printf()) will be
  * output to wherever Pd thinks it has too (pd's console, the stderr...)
  * it automatically adds a newline at the end of the string
  */
  post("Tactile Audio: Powerfully expressive musical instruments!");
}

void fbnet_tilde_test(t_fbnet_tilde *x, t_float argument)
{
  post("Got %f", argument);
}

/**
 * set feedback gain of internal delays
 */
void set_igain(t_fbnet_tilde *x, t_float igain)
{
  x->igain = igain;
}

/**
 * set gain of crossfeed between internal delays
 */
void set_xgain(t_fbnet_tilde *x, t_float xgain)
{
  x->xgain = xgain;
}

/**
 * this is the "destructor" of the class;
 * it allows us to free dynamically allocated ressources
 */
void fbnet_tilde_free(t_fbnet_tilde *x)
{
  /* free delay memory */
  freebytes(x->n1_delay_1.memory, x->n1_delay_1.size);
  freebytes(x->n2_delay_1.memory, x->n2_delay_1.size);
  freebytes(x->n3_delay_1.memory, x->n3_delay_1.size);
  freebytes(x->n4_delay_1.memory, x->n4_delay_1.size);

  /* free any ressources associated with the given inlet */
  inlet_free(x->x_in2);
  inlet_free(x->x_in3);

  /* free any ressources associated with the given outlet */
  outlet_free(x->x_out1);
}

/**
 * this is the "constructor" of the class
 * this method is called whenever a new object of this class is created
 * the name of this function is arbitrary and is registered to Pd in the 
 * fbnet_tilde_setup() routine
 */
void *fbnet_tilde_new(void)
{
  /*
   * call the "constructor" of the parent-class
   * this will reserve enough memory to hold "t_fbnet_tilde"
   */
  t_fbnet_tilde *x = (t_fbnet_tilde *)pd_new(fbnet_tilde_class);

  /* create a new signal intlet */
  // signalinlet_new() ??
  x->x_in2 = inlet_new(&x->x_obj, &x->x_obj.ob_pd, &s_signal, &s_signal);
  x->x_in3 = inlet_new(&x->x_obj, &x->x_obj.ob_pd, &s_signal, &s_signal);
  x->x_f = 0;

  /* create a new signal outlet */
  x->x_out1 = outlet_new(&x->x_obj, &s_signal);

  /* fbnet */
  x->xgain = 0;
  x->igain = 0;

  /* initialize dc blockers */
  dcblock_reset(&x->n1_dcblock_1);
  dcblock_reset(&x->n1_dcblock_2);
  dcblock_reset(&x->n2_dcblock_1);
  dcblock_reset(&x->n2_dcblock_2);
  dcblock_reset(&x->n3_dcblock_1);
  dcblock_reset(&x->n4_dcblock_1);

  /* allocate delay memory 
   *
   * d_delay.c implements methods for changing the samplingrate. 
   * might become necessary if we decide to go with sr > 48kHz.
   * 
   */
  // node 1
  x->n1_delay_1.size = 32768;
  x->n1_delay_1.memory = (t_sample *)getbytes(x->n1_delay_1.size * sizeof(t_sample));
  x->n1_delay_1.reader = 0;
  x->n1_delay_1.writer = 0;
  x->n1_delay_1.wrap = 0;
  x->n1_x = 0;
  delay_reset(&x->n1_delay_1);

  // node 2
  x->n2_delay_1.size = 32768;
  x->n2_delay_1.memory = (t_sample *)getbytes(x->n2_delay_1.size * sizeof(t_sample));
  x->n2_delay_1.reader = 0;
  x->n2_delay_1.writer = 0;
  x->n2_delay_1.wrap = 0;
  x->n2_x = 0;
  delay_reset(&x->n2_delay_1);

  // node 3
  x->n3_delay_1.size = 65536;
  x->n3_delay_1.memory = (t_sample *)getbytes(x->n3_delay_1.size * sizeof(t_sample));
  x->n3_delay_1.reader = 0;
  x->n3_delay_1.writer = 0;
  x->n3_delay_1.wrap = 0;
  x->n3_x = 0;
  delay_reset(&x->n3_delay_1);

  // node 4
  x->n4_delay_1.size = 65536;
  x->n4_delay_1.memory = (t_sample *)getbytes(x->n4_delay_1.size * sizeof(t_sample));
  x->n4_delay_1.reader = 0;
  x->n4_delay_1.writer = 0;
  x->n4_delay_1.wrap = 0;
  x->n4_x = 0;
  delay_reset(&x->n4_delay_1);

  /* initialize filters */
  lowpass_reset(&x->n1_lowpass_1);
  lowpass_reset(&x->n2_lowpass_1);
  lowpass_reset(&x->n3_lowpass_1);
  lowpass_reset(&x->n4_lowpass_1);

  /*
   * return the pointer to the class - this is mandatory
   * if you return "0", then the object-creation will fail
   */
  return (void *)x;
}

/**
 * define the function-space of the class
 * within a single-object external the name of this function is special
 */
void fbnet_tilde_setup(void)
{
  /* create a new class */
  fbnet_tilde_class = class_new(gensym("fbnet~"), /* the object's name is "fbnet_tilde" */
    (t_newmethod)fbnet_tilde_new, /* the object's constructor is "fbnet_tilde_new()" */
    (t_method)fbnet_tilde_free,   /* no special destructor */
    sizeof(t_fbnet_tilde),        /* the size of the data-space */
    CLASS_DEFAULT,                /* a normal pd object */
    0);                           /* no creation arguments */
  
  /* attach functions to messages */
  class_addbang(fbnet_tilde_class, fbnet_tilde_bang);
  class_addmethod(fbnet_tilde_class, (t_method)fbnet_tilde_test, gensym("test"), A_FLOAT, 0);

  /* attach functions to set params from messages (igain and xgain, later reset and clear) */
  class_addmethod(fbnet_tilde_class, (t_method)set_igain, gensym("igain"), A_FLOAT, 0);
  class_addmethod(fbnet_tilde_class, (t_method)set_xgain, gensym("xgain"), A_FLOAT, 0);

  /* declare the first inlet as a signal inlet */
  CLASS_MAINSIGNALIN(fbnet_tilde_class, t_fbnet_tilde, x_f);

  class_addmethod(fbnet_tilde_class, (t_method)fbnet_tilde_dsp, gensym("dsp"), A_CANT, 0);
}
