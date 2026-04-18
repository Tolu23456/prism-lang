#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

#include "builtins.h"
#include "value.h"
#include "interpreter.h"
#include "gc.h"

/* ================================================================== error callback */

static void (*g_throw_cb)(const char *msg) = NULL;

void prism_set_builtin_throw(void (*cb)(const char *msg)) {
    g_throw_cb = cb;
}

static void do_throw(const char *msg) {
    if (g_throw_cb) g_throw_cb(msg);
    else { fprintf(stderr, "RuntimeError: %s\n", msg); exit(1); }
}

/* ================================================================== core I/O */

static Value *bi_output(Value **args, int argc) {
    for (int i = 0; i < argc; i++) {
        if (i > 0) printf(" ");
        if (args[i]->type == VAL_STRING)
            printf("%s", args[i]->str_val);
        else
            value_print(args[i]);
    }
    printf("\n");
    return value_null();
}

static Value *bi_print(Value **args, int argc) {
    for (int i = 0; i < argc; i++) {
        if (i > 0) printf(" ");
        char *s = value_to_string(args[i]);
        printf("%s", s);
        free(s);
    }
    printf("\n");
    return value_null();
}

static Value *bi_printf_fn(Value **args, int argc) {
    /* printf(fmt, arg1, arg2, ...) — simple %s/%d/%f substitution */
    if (argc < 1 || args[0]->type != VAL_STRING) return value_null();
    const char *fmt = args[0]->str_val;
    int ai = 1;
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') { putchar(*p); continue; }
        p++;
        switch (*p) {
            case 's': if (ai < argc) { char *s = value_to_string(args[ai++]); printf("%s", s); free(s); } break;
            case 'd': if (ai < argc) { long long v = (args[ai]->type==VAL_INT)?args[ai]->int_val:(long long)args[ai]->float_val; ai++; printf("%lld", v); } break;
            case 'f': if (ai < argc) { double v = (args[ai]->type==VAL_FLOAT)?args[ai]->float_val:(double)args[ai]->int_val; ai++; printf("%f", v); } break;
            case '%': putchar('%'); break;
            default: putchar('%'); putchar(*p); break;
        }
    }
    return value_null();
}

static Value *bi_input(Value **args, int argc) {
    if (argc > 0 && args[0]->type == VAL_STRING)
        printf("%s", args[0]->str_val);
    fflush(stdout);
    char buf[4096];
    if (!fgets(buf, sizeof(buf), stdin)) return value_string("");
    size_t len = strlen(buf);
    if (len > 0 && buf[len-1] == '\n') buf[len-1] = '\0';
    return value_string(buf);
}

/* ================================================================== type functions */

static Value *bi_len(Value **args, int argc) {
    if (argc < 1) return value_int(0);
    Value *v = args[0];
    switch (v->type) {
        case VAL_STRING: return value_int((long long)strlen(v->str_val));
        case VAL_ARRAY:  return value_int(v->array.len);
        case VAL_TUPLE:  return value_int(v->tuple.len);
        case VAL_DICT:   return value_int(v->dict.len);
        case VAL_SET:    return value_int(v->set.len);
        default:         return value_int(0);
    }
}

static Value *bi_bool_fn(Value **args, int argc) {
    if (argc < 1) return value_bool(0);
    Value *v = args[0];
    if (v->type == VAL_BOOL) return value_retain(v);
    if (v->type == VAL_INT)  return value_bool(v->int_val == 0 ? 0 : (v->int_val < 0 ? -1 : 1));
    if (v->type == VAL_STRING) {
        if (strcmp(v->str_val, "true")    == 0) return value_bool(1);
        if (strcmp(v->str_val, "false")   == 0) return value_bool(0);
        if (strcmp(v->str_val, "unknown") == 0) return value_bool(-1);
    }
    return value_bool(value_truthy(v) ? 1 : 0);
}

static Value *bi_int_fn(Value **args, int argc) {
    if (argc < 1) return value_int(0);
    Value *v = args[0];
    if (v->type == VAL_INT)     return value_retain(v);
    if (v->type == VAL_FLOAT)   return value_int((long long)v->float_val);
    if (v->type == VAL_BOOL)    return value_int(v->bool_val == 1 ? 1 : 0);
    if (v->type == VAL_NULL)    return value_int(0);
    if (v->type == VAL_COMPLEX) return value_int((long long)v->complex_val.real);
    if (v->type == VAL_STRING) {
        const char *s = v->str_val;
        if (s[0]=='0' && (s[1]=='b'||s[1]=='B')) return value_int(strtoll(s+2,NULL,2));
        if (s[0]=='0' && (s[1]=='o'||s[1]=='O')) return value_int(strtoll(s+2,NULL,8));
        return value_int(strtoll(s, NULL, 0));
    }
    return value_int(0);
}

static Value *bi_float_fn(Value **args, int argc) {
    if (argc < 1) return value_float(0.0);
    Value *v = args[0];
    if (v->type == VAL_FLOAT)   return value_retain(v);
    if (v->type == VAL_INT)     return value_float((double)v->int_val);
    if (v->type == VAL_BOOL)    return value_float(v->bool_val==1?1.0:0.0);
    if (v->type == VAL_NULL)    return value_float(0.0);
    if (v->type == VAL_COMPLEX) return value_float(v->complex_val.real);
    if (v->type == VAL_STRING)  return value_float(strtod(v->str_val, NULL));
    return value_float(0.0);
}

static Value *bi_str_fn(Value **args, int argc) {
    if (argc < 1) return value_string("");
    char *s = value_to_string(args[0]);
    return value_string_take(s);
}

static Value *bi_dict_fn(Value **args, int argc) {
    Value *d = value_dict_new();
    if (argc == 1 && args[0]->type == VAL_DICT) {
        for (int i = 0; i < args[0]->dict.len; i++)
            value_dict_set(d, args[0]->dict.entries[i].key, args[0]->dict.entries[i].val);
    } else if (argc % 2 == 0) {
        for (int i = 0; i < argc; i += 2)
            value_dict_set(d, args[i], args[i+1]);
    }
    return d;
}

static Value *bi_set_fn(Value **args, int argc) {
    Value *s = value_set_new();
    if (argc >= 1) {
        Value *src = args[0];
        if (src->type == VAL_ARRAY || src->type == VAL_TUPLE) {
            ValueArray *a = (src->type==VAL_ARRAY)?&src->array:&src->tuple;
            for (int i = 0; i < a->len; i++) value_set_add(s, a->items[i]);
        } else if (src->type == VAL_SET) {
            for (int i = 0; i < src->set.len; i++) value_set_add(s, src->set.items[i]);
        } else if (src->type == VAL_DICT) {
            for (int i = 0; i < src->dict.len; i++) value_set_add(s, src->dict.entries[i].key);
        } else if (src->type == VAL_STRING) {
            const char *p = src->str_val;
            while (*p) { char buf[2]={*p++,'\0'}; value_set_add(s, value_string(buf)); }
        }
    }
    return s;
}

static Value *bi_array_fn(Value **args, int argc) {
    Value *a = value_array_new();
    if (argc >= 1) {
        Value *src = args[0];
        if (src->type == VAL_ARRAY) {
            for (int i = 0; i < src->array.len; i++) value_array_push(a, src->array.items[i]);
        } else if (src->type == VAL_TUPLE) {
            for (int i = 0; i < src->tuple.len; i++) value_array_push(a, src->tuple.items[i]);
        } else if (src->type == VAL_SET) {
            for (int i = 0; i < src->set.len; i++) value_array_push(a, src->set.items[i]);
        } else if (src->type == VAL_DICT) {
            for (int i = 0; i < src->dict.len; i++) value_array_push(a, src->dict.entries[i].key);
        } else if (src->type == VAL_STRING) {
            const char *p = src->str_val;
            while (*p) { char buf[2]={*p++,'\0'}; value_array_push(a, value_string(buf)); }
        } else {
            value_array_push(a, src);
        }
    }
    return a;
}

static Value *bi_tuple_fn(Value **args, int argc) {
    if (argc < 1) return value_tuple_new(NULL, 0);
    Value *src = args[0];
    if (src->type == VAL_TUPLE) return value_retain(src);
    if (src->type == VAL_ARRAY) return value_tuple_new(src->array.items, src->array.len);
    if (src->type == VAL_SET)   return value_tuple_new(src->set.items,   src->set.len);
    if (src->type == VAL_DICT) {
        Value **ks = malloc(src->dict.len * sizeof(Value *));
        for (int i = 0; i < src->dict.len; i++) ks[i] = src->dict.entries[i].key;
        Value *t = value_tuple_new(ks, src->dict.len);
        free(ks);
        return t;
    }
    return value_tuple_new(&src, 1);
}

static Value *bi_complex_fn(Value **args, int argc) {
    if (argc < 1) return value_complex(0.0, 0.0);
    double real = 0.0, imag = 0.0;
    Value *v = args[0];
    if (v->type == VAL_COMPLEX)  return value_retain(v);
    if (v->type == VAL_INT)      real = (double)v->int_val;
    else if (v->type == VAL_FLOAT) real = v->float_val;
    if (argc >= 2) {
        Value *v2 = args[1];
        if (v2->type == VAL_INT)   imag = (double)v2->int_val;
        else if (v2->type == VAL_FLOAT) imag = v2->float_val;
    }
    return value_complex(real, imag);
}

static Value *bi_type_fn(Value **args, int argc) {
    if (argc < 1) return value_string("null");
    return value_string(value_type_name(args[0]->type));
}

/* ================================================================== assertions */

static Value *bi_assert(Value **args, int argc) {
    if (argc < 1 || !value_truthy(args[0])) {
        const char *msg = (argc > 1 && args[1]->type == VAL_STRING)
            ? args[1]->str_val : "assertion failed";
        fprintf(stderr, "[FAIL] %s\n", msg);
        exit(1);
    }
    return value_null();
}

static Value *bi_assert_eq(Value **args, int argc) {
    if (argc < 2) {
        fprintf(stderr, "[FAIL] assert_eq requires 2 arguments\n");
        exit(1);
    }
    if (!value_equals(args[0], args[1])) {
        if (argc > 2 && args[2]->type == VAL_STRING) {
            fprintf(stderr, "[FAIL] %s\n", args[2]->str_val);
        } else {
            char *s0 = value_to_string(args[0]);
            char *s1 = value_to_string(args[1]);
            fprintf(stderr, "[FAIL] expected %s but got %s\n", s1, s0);
            free(s0); free(s1);
        }
        exit(1);
    }
    return value_null();
}

/* ================================================================== math */

static Value *bi_abs(Value **a, int n) {
    if (n < 1) return value_int(0);
    if (a[0]->type == VAL_INT)   return value_int(llabs(a[0]->int_val));
    if (a[0]->type == VAL_FLOAT) return value_float(fabs(a[0]->float_val));
    return value_retain(a[0]);
}
static Value *bi_sqrt(Value **a, int n) {
    if (n < 1) return value_float(0.0);
    double v = (a[0]->type==VAL_INT)?(double)a[0]->int_val:a[0]->float_val;
    return value_float(sqrt(v));
}
static Value *bi_floor(Value **a, int n) {
    if (n < 1) return value_int(0);
    if (a[0]->type == VAL_INT) return value_int(a[0]->int_val);
    return value_int((long long)floor(a[0]->float_val));
}
static Value *bi_ceil(Value **a, int n) {
    if (n < 1) return value_int(0);
    if (a[0]->type == VAL_INT) return value_int(a[0]->int_val);
    return value_int((long long)ceil(a[0]->float_val));
}
static Value *bi_round(Value **a, int n) {
    if (n < 1) return value_int(0);
    if (a[0]->type == VAL_INT) return value_int(a[0]->int_val);
    int digits = (n >= 2 && a[1]->type == VAL_INT) ? (int)a[1]->int_val : 0;
    if (digits == 0) return value_int((long long)round(a[0]->float_val));
    double mult = pow(10.0, (double)digits);
    return value_float(round(a[0]->float_val * mult) / mult);
}
static Value *bi_pow(Value **a, int n) {
    if (n < 2) return value_float(0.0);
    double base = (a[0]->type==VAL_INT)?(double)a[0]->int_val:a[0]->float_val;
    double exp  = (a[1]->type==VAL_INT)?(double)a[1]->int_val:a[1]->float_val;
    double r = pow(base, exp);
    if (a[0]->type==VAL_INT && a[1]->type==VAL_INT && exp>=0) return value_int((long long)r);
    return value_float(r);
}
static double _num(Value *v) {
    if (v->type==VAL_INT) return (double)v->int_val;
    if (v->type==VAL_FLOAT) return v->float_val;
    return 0.0;
}
static Value *bi_sin(Value **a,int n){if(n<1)return value_float(0.0);return value_float(sin(_num(a[0])));}
static Value *bi_cos(Value **a,int n){if(n<1)return value_float(1.0);return value_float(cos(_num(a[0])));}
static Value *bi_tan(Value **a,int n){if(n<1)return value_float(0.0);return value_float(tan(_num(a[0])));}
static Value *bi_asin(Value **a,int n){if(n<1)return value_float(0.0);return value_float(asin(_num(a[0])));}
static Value *bi_acos(Value **a,int n){if(n<1)return value_float(0.0);return value_float(acos(_num(a[0])));}
static Value *bi_atan(Value **a,int n){if(n<1)return value_float(0.0);return value_float(atan(_num(a[0])));}
static Value *bi_atan2(Value **a,int n){if(n<2)return value_float(0.0);return value_float(atan2(_num(a[0]),_num(a[1])));}
static Value *bi_log(Value **a,int n){
    if(n<1)return value_float(0.0);
    if(n>=2){return value_float(log(_num(a[0]))/log(_num(a[1])));}
    return value_float(log(_num(a[0])));
}
static Value *bi_log2(Value **a,int n){if(n<1)return value_float(0.0);return value_float(log2(_num(a[0])));}
static Value *bi_log10(Value **a,int n){if(n<1)return value_float(0.0);return value_float(log10(_num(a[0])));}
static Value *bi_exp(Value **a,int n){if(n<1)return value_float(1.0);return value_float(exp(_num(a[0])));}
static Value *bi_hypot(Value **a,int n){if(n<2)return value_float(0.0);return value_float(hypot(_num(a[0]),_num(a[1])));}
static Value *bi_isnan(Value **a,int n){if(n<1)return value_bool(0);return value_bool((a[0]->type==VAL_FLOAT&&isnan(a[0]->float_val))?1:0);}
static Value *bi_isinf(Value **a,int n){if(n<1)return value_bool(0);return value_bool((a[0]->type==VAL_FLOAT&&isinf(a[0]->float_val))?1:0);}
static Value *bi_min(Value **a,int n){
    if(n==0)return value_null();
    if(n==1&&a[0]->type==VAL_ARRAY){
        if(a[0]->array.len==0)return value_null();
        Value *m=a[0]->array.items[0];
        for(int i=1;i<a[0]->array.len;i++) if(value_compare(a[0]->array.items[i],m)<0) m=a[0]->array.items[i];
        return value_retain(m);
    }
    Value *m=a[0]; for(int i=1;i<n;i++) if(value_compare(a[i],m)<0) m=a[i];
    return value_retain(m);
}
static Value *bi_max(Value **a,int n){
    if(n==0)return value_null();
    if(n==1&&a[0]->type==VAL_ARRAY){
        if(a[0]->array.len==0)return value_null();
        Value *m=a[0]->array.items[0];
        for(int i=1;i<a[0]->array.len;i++) if(value_compare(a[0]->array.items[i],m)>0) m=a[0]->array.items[i];
        return value_retain(m);
    }
    Value *m=a[0]; for(int i=1;i<n;i++) if(value_compare(a[i],m)>0) m=a[i];
    return value_retain(m);
}
static Value *bi_clamp(Value **a,int n){
    if(n<3)return n>0?value_retain(a[0]):value_null();
    if(value_compare(a[0],a[1])<0)return value_retain(a[1]);
    if(value_compare(a[0],a[2])>0)return value_retain(a[2]);
    return value_retain(a[0]);
}
static Value *bi_sum(Value **a,int n){
    if(n<1)return value_int(0);
    if(a[0]->type==VAL_ARRAY){
        long long isum=0; double fsum=0.0; bool use_float=false;
        for(int i=0;i<a[0]->array.len;i++){
            Value *v=a[0]->array.items[i];
            if(v->type==VAL_FLOAT){use_float=true;fsum+=v->float_val;}
            else if(v->type==VAL_INT){isum+=v->int_val;fsum+=(double)v->int_val;}
        }
        return use_float?value_float(fsum):value_int(isum);
    }
    return value_int(0);
}

/* ================================================================== time */

static Value *bi_clock(Value **a, int n) {
    (void)a;(void)n;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return value_float((double)ts.tv_sec + (double)ts.tv_nsec*1e-9);
}
static Value *bi_time_now(Value **a, int n) {
    (void)a;(void)n;
    return value_float((double)time(NULL));
}

/* ================================================================== string functions */

static Value *bi_chars(Value **a, int n) {
    if (n<1||a[0]->type!=VAL_STRING) return value_array_new();
    const char *s=a[0]->str_val;
    Value *arr=value_array_new();
    for(const char *p=s;*p;p++){
        char buf[2]={*p,'\0'};
        Value *cv=value_string(buf); value_array_push(arr,cv); value_release(cv);
    }
    return arr;
}
static Value *bi_upper(Value **a, int n) {
    if(n<1||a[0]->type!=VAL_STRING)return n>0?value_retain(a[0]):value_string("");
    char *r=strdup(a[0]->str_val);
    for(char *p=r;*p;p++) *p=toupper((unsigned char)*p);
    return value_string_take(r);
}
static Value *bi_lower(Value **a, int n) {
    if(n<1||a[0]->type!=VAL_STRING)return n>0?value_retain(a[0]):value_string("");
    char *r=strdup(a[0]->str_val);
    for(char *p=r;*p;p++) *p=tolower((unsigned char)*p);
    return value_string_take(r);
}
static Value *bi_trim(Value **a, int n) {
    if(n<1||a[0]->type!=VAL_STRING)return n>0?value_retain(a[0]):value_string("");
    const char *s=a[0]->str_val;
    while(isspace((unsigned char)*s))s++;
    const char *e=s+strlen(s);
    while(e>s&&isspace((unsigned char)*(e-1)))e--;
    return value_string(strndup(s,(size_t)(e-s)));
}
static Value *bi_ltrim(Value **a, int n) {
    if(n<1||a[0]->type!=VAL_STRING)return n>0?value_retain(a[0]):value_string("");
    const char *s=a[0]->str_val;
    while(isspace((unsigned char)*s))s++;
    return value_string(s);
}
static Value *bi_rtrim(Value **a, int n) {
    if(n<1||a[0]->type!=VAL_STRING)return n>0?value_retain(a[0]):value_string("");
    const char *s=a[0]->str_val;
    const char *e=s+strlen(s);
    while(e>s&&isspace((unsigned char)*(e-1)))e--;
    return value_string(strndup(s,(size_t)(e-s)));
}
static Value *bi_starts(Value **a, int n) {
    if(n<2||a[0]->type!=VAL_STRING||a[1]->type!=VAL_STRING)return value_bool(0);
    return value_bool(strncmp(a[0]->str_val,a[1]->str_val,strlen(a[1]->str_val))==0?1:0);
}
static Value *bi_ends(Value **a, int n) {
    if(n<2||a[0]->type!=VAL_STRING||a[1]->type!=VAL_STRING)return value_bool(0);
    size_t sl=strlen(a[0]->str_val),pl=strlen(a[1]->str_val);
    return value_bool((sl>=pl&&strcmp(a[0]->str_val+sl-pl,a[1]->str_val)==0)?1:0);
}
static Value *bi_contains(Value **a, int n) {
    if(n<2)return value_bool(0);
    if(a[0]->type==VAL_STRING&&a[1]->type==VAL_STRING)
        return value_bool(strstr(a[0]->str_val,a[1]->str_val)?1:0);
    if(a[0]->type==VAL_ARRAY){
        for(int i=0;i<a[0]->array.len;i++)
            if(value_equals(a[0]->array.items[i],a[1]))return value_bool(1);
        return value_bool(0);
    }
    if(a[0]->type==VAL_DICT){ Value *f=value_dict_get(a[0],a[1]); return value_bool(f?1:0); }
    return value_bool(0);
}

static Value *bi_starts_with(Value **a, int n) {
    if(n<2||a[0]->type!=VAL_STRING||a[1]->type!=VAL_STRING) return value_bool(0);
    size_t pl = strlen(a[1]->str_val);
    return value_bool(strncmp(a[0]->str_val, a[1]->str_val, pl) == 0 ? 1 : 0);
}

static Value *bi_ends_with(Value **a, int n) {
    if(n<2||a[0]->type!=VAL_STRING||a[1]->type!=VAL_STRING) return value_bool(0);
    size_t sl = strlen(a[0]->str_val);
    size_t pl = strlen(a[1]->str_val);
    if(pl > sl) return value_bool(0);
    return value_bool(strcmp(a[0]->str_val + sl - pl, a[1]->str_val) == 0 ? 1 : 0);
}

static Value *bi_floor_div(Value **a, int n) {
    if(n<2) return value_null();
    if(a[0]->type==VAL_INT && a[1]->type==VAL_INT) {
        if(a[1]->int_val==0) { fprintf(stderr,"[prism] // by zero\n"); return value_null(); }
        long long q = a[0]->int_val / a[1]->int_val;
        if((a[0]->int_val ^ a[1]->int_val) < 0 && q * a[1]->int_val != a[0]->int_val) q--;
        return value_int(q);
    }
    double fa = (a[0]->type==VAL_INT)?(double)a[0]->int_val:a[0]->float_val;
    double fb = (a[1]->type==VAL_INT)?(double)a[1]->int_val:a[1]->float_val;
    if(fb==0.0) { fprintf(stderr,"[prism] // by zero\n"); return value_null(); }
    return value_float(floor(fa/fb));
}
static Value *bi_split(Value **a, int n) {
    if(n<1||a[0]->type!=VAL_STRING)return value_array_new();
    const char *s=a[0]->str_val;
    const char *delim=(n>=2&&a[1]->type==VAL_STRING)?a[1]->str_val:" ";
    Value *arr=value_array_new();
    size_t dlen=strlen(delim);
    if(dlen==0){
        for(const char *p=s;*p;p++){
            char buf[2]={*p,'\0'};
            Value *sv=value_string(buf); value_array_push(arr,sv); value_release(sv);
        }
        return arr;
    }
    char *copy=strdup(s),*rest=copy,*pos;
    while((pos=strstr(rest,delim))!=NULL){
        *pos='\0'; Value *sv=value_string(rest); value_array_push(arr,sv); value_release(sv);
        rest=pos+dlen;
    }
    Value *sv=value_string(rest); value_array_push(arr,sv); value_release(sv);
    free(copy);
    return arr;
}
static Value *bi_join(Value **a, int n) {
    const char *sep=""; Value *arr=NULL;
    if(n>=2){
        if(a[0]->type==VAL_STRING&&a[1]->type==VAL_ARRAY){sep=a[0]->str_val;arr=a[1];}
        else if(a[0]->type==VAL_ARRAY&&a[1]->type==VAL_STRING){arr=a[0];sep=a[1]->str_val;}
        else if(a[0]->type==VAL_ARRAY){arr=a[0];}
    } else if(n==1&&a[0]->type==VAL_ARRAY){arr=a[0];}
    if(!arr)return value_string("");
    size_t dlen=strlen(sep);
    int cap=256,sz=0;
    char *res=malloc(cap); res[0]='\0';
    for(int i=0;i<arr->array.len;i++){
        char *part=value_to_string(arr->array.items[i]);
        size_t plen=strlen(part);
        while(sz+(int)plen+(int)dlen+2>=cap){cap*=2;res=realloc(res,cap);}
        memcpy(res+sz,part,plen);sz+=(int)plen;res[sz]='\0';
        if(i<arr->array.len-1&&dlen>0){memcpy(res+sz,sep,dlen);sz+=(int)dlen;res[sz]='\0';}
        free(part);
    }
    return value_string_take(res);
}
static Value *bi_replace(Value **a, int n) {
    if(n<3||a[0]->type!=VAL_STRING||a[1]->type!=VAL_STRING||a[2]->type!=VAL_STRING)
        return n>0?value_retain(a[0]):value_string("");
    const char *s=a[0]->str_val,*old=a[1]->str_val,*neww=a[2]->str_val;
    size_t oldlen=strlen(old),newlen=strlen(neww);
    int cap=256,sz=0;
    char *res=malloc(cap); res[0]='\0';
    const char *p=s;
    while(*p){
        if(oldlen>0&&strncmp(p,old,oldlen)==0){
            while(sz+(int)newlen+1>=cap){cap*=2;res=realloc(res,cap);}
            memcpy(res+sz,neww,newlen);sz+=(int)newlen;res[sz]='\0';
            p+=oldlen;
        } else {
            if(sz+1>=cap){cap*=2;res=realloc(res,cap);}
            res[sz++]=*p++;res[sz]='\0';
        }
    }
    return value_string_take(res);
}
static Value *bi_fromCharCode(Value **a, int n) {
    if(n<1)return value_string("");
    char buf[2]={0,0};
    long long code=(a[0]->type==VAL_INT)?a[0]->int_val:(long long)a[0]->float_val;
    buf[0]=(char)(code&0xFF);
    return value_string(buf);
}
static Value *bi_ord(Value **a, int n) {
    if(n<1||a[0]->type!=VAL_STRING||a[0]->str_val[0]=='\0')return value_int(0);
    return value_int((long long)(unsigned char)a[0]->str_val[0]);
}
static Value *bi_parseInt(Value **a, int n) {
    if(n<1)return value_int(0);
    if(a[0]->type==VAL_INT)return value_retain(a[0]);
    if(a[0]->type==VAL_FLOAT)return value_int((long long)a[0]->float_val);
    if(a[0]->type==VAL_STRING){
        int base=(n>=2&&a[1]->type==VAL_INT)?(int)a[1]->int_val:10;
        return value_int(strtoll(a[0]->str_val,NULL,base));
    }
    return value_int(0);
}
static Value *bi_parseFloat(Value **a, int n) {
    if(n<1)return value_float(0.0);
    if(a[0]->type==VAL_FLOAT)return value_retain(a[0]);
    if(a[0]->type==VAL_INT)return value_float((double)a[0]->int_val);
    if(a[0]->type==VAL_STRING)return value_float(strtod(a[0]->str_val,NULL));
    return value_float(0.0);
}
static Value *bi_hex(Value **a, int n) {
    if(n<1||a[0]->type!=VAL_INT)return value_string("0x0");
    char buf[32]; snprintf(buf,sizeof(buf),"0x%llx",(unsigned long long)a[0]->int_val);
    return value_string(buf);
}
static Value *bi_bin(Value **a, int n) {
    if(n<1||a[0]->type!=VAL_INT)return value_string("0b0");
    long long v=a[0]->int_val;
    if(v==0)return value_string("0b0");
    char buf[70]; int pos=68;
    buf[69]='\0';
    unsigned long long uv=(unsigned long long)v;
    buf[pos]='\0';
    while(uv>0){buf[--pos]=(uv&1)?'1':'0';uv>>=1;}
    buf[--pos]='b'; buf[--pos]='0';
    return value_string(buf+pos);
}
static Value *bi_oct(Value **a, int n) {
    if(n<1||a[0]->type!=VAL_INT)return value_string("0o0");
    char buf[32]; snprintf(buf,sizeof(buf),"0o%llo",(unsigned long long)a[0]->int_val);
    return value_string(buf);
}
static Value *bi_repeat(Value **a, int n) {
    if(n<2||a[0]->type!=VAL_STRING||a[1]->type!=VAL_INT)
        return n>0?value_retain(a[0]):value_string("");
    const char *s=a[0]->str_val;
    long long count=a[1]->int_val;
    if(count<=0)return value_string("");
    size_t slen=strlen(s);
    size_t total=slen*(size_t)count;
    char *buf=malloc(total+1);
    for(long long i=0;i<count;i++) memcpy(buf+i*slen,s,slen);
    buf[total]='\0';
    return value_string_take(buf);
}
static Value *bi_pad_left(Value **a, int n) {
    if(n<2||a[0]->type!=VAL_STRING||a[1]->type!=VAL_INT)return n>0?value_retain(a[0]):value_string("");
    const char *s=a[0]->str_val; long long width=a[1]->int_val;
    char pad=' ';
    if(n>=3&&a[2]->type==VAL_STRING&&a[2]->str_val[0]) pad=a[2]->str_val[0];
    long long slen=(long long)strlen(s);
    if(slen>=width)return value_retain(a[0]);
    long long pad_n=width-slen;
    char *buf=malloc((size_t)(width+1));
    for(long long i=0;i<pad_n;i++) buf[i]=pad;
    memcpy(buf+pad_n,s,(size_t)slen); buf[width]='\0';
    return value_string_take(buf);
}
static Value *bi_pad_right(Value **a, int n) {
    if(n<2||a[0]->type!=VAL_STRING||a[1]->type!=VAL_INT)return n>0?value_retain(a[0]):value_string("");
    const char *s=a[0]->str_val; long long width=a[1]->int_val;
    char pad=' ';
    if(n>=3&&a[2]->type==VAL_STRING&&a[2]->str_val[0]) pad=a[2]->str_val[0];
    long long slen=(long long)strlen(s);
    if(slen>=width)return value_retain(a[0]);
    long long pad_n=width-slen;
    char *buf=malloc((size_t)(width+1));
    memcpy(buf,s,(size_t)slen);
    for(long long i=0;i<pad_n;i++) buf[slen+i]=pad;
    buf[width]='\0';
    return value_string_take(buf);
}
static Value *bi_index_of(Value **a, int n) {
    if(n<2)return value_int(-1);
    if(a[0]->type==VAL_STRING&&a[1]->type==VAL_STRING){
        const char *pos=strstr(a[0]->str_val,a[1]->str_val);
        return value_int(pos?pos-a[0]->str_val:-1);
    }
    if(a[0]->type==VAL_ARRAY){
        for(int i=0;i<a[0]->array.len;i++)
            if(value_equals(a[0]->array.items[i],a[1]))return value_int(i);
    }
    return value_int(-1);
}
static Value *bi_count_in(Value **a, int n) {
    if(n<2)return value_int(0);
    if(a[0]->type==VAL_STRING&&a[1]->type==VAL_STRING){
        const char *s=a[0]->str_val,*sub=a[1]->str_val;
        size_t sublen=strlen(sub); long long c=0;
        if(sublen==0)return value_int(0);
        for(const char *p=s;(p=strstr(p,sub));p+=sublen) c++;
        return value_int(c);
    }
    if(a[0]->type==VAL_ARRAY){
        long long c=0;
        for(int i=0;i<a[0]->array.len;i++) if(value_equals(a[0]->array.items[i],a[1])) c++;
        return value_int(c);
    }
    return value_int(0);
}

/* ================================================================== array functions */

static Value *bi_push(Value **a, int n) {
    if(n<2||a[0]->type!=VAL_ARRAY)return value_null();
    for(int i=1;i<n;i++) value_array_push(a[0],a[i]);
    return value_retain(a[0]);
}
static Value *bi_pop(Value **a, int n) {
    if(n<1||a[0]->type!=VAL_ARRAY||a[0]->array.len==0)return value_null();
    long long idx=(n>=2&&a[1]->type==VAL_INT)?a[1]->int_val:(long long)(a[0]->array.len-1);
    return value_array_pop(a[0],idx);
}
static Value *bi_sort(Value **a, int n) {
    if(n<1||a[0]->type!=VAL_ARRAY)return n>0?value_retain(a[0]):value_array_new();
    value_array_sort(a[0]);
    return value_retain(a[0]);
}
static Value *bi_sorted(Value **a, int n) {
    if(n<1||a[0]->type!=VAL_ARRAY)return value_array_new();
    Value *copy=value_copy(a[0]);
    value_array_sort(copy);
    return copy;
}
static Value *bi_reverse(Value **a, int n) {
    if(n<1)return value_array_new();
    if(a[0]->type==VAL_ARRAY){
        ValueArray *arr=&a[0]->array;
        for(int i=0,j=arr->len-1;i<j;i++,j--){
            Value *tmp=arr->items[i]; arr->items[i]=arr->items[j]; arr->items[j]=tmp;
        }
        return value_retain(a[0]);
    }
    if(a[0]->type==VAL_STRING){
        const char *s=a[0]->str_val; size_t len=strlen(s);
        char *r=malloc(len+1);
        for(size_t i=0;i<len;i++) r[i]=s[len-1-i];
        r[len]='\0';
        return value_string_take(r);
    }
    return value_retain(a[0]);
}
static Value *bi_slice(Value **a, int n) {
    if(n<1)return value_array_new();
    long long start=(n>=2&&a[1]->type==VAL_INT)?a[1]->int_val:0;
    if(a[0]->type==VAL_STRING){
        const char *s=a[0]->str_val; long long slen=(long long)strlen(s);
        long long end=(n>=3&&a[2]->type==VAL_INT)?a[2]->int_val:slen;
        if(start<0)start=slen+start;
        if(end<0)end=slen+end;
        if(start<0)start=0;
        if(end>slen)end=slen;
        if(start>=end)return value_string("");
        return value_string(strndup(s+(size_t)start,(size_t)(end-start)));
    }
    if(a[0]->type==VAL_ARRAY){
        long long alen=a[0]->array.len;
        long long end=(n>=3&&a[2]->type==VAL_INT)?a[2]->int_val:alen;
        if(start<0)start=alen+start;
        if(end<0)end=alen+end;
        if(start<0)start=0;
        if(end>alen)end=alen;
        Value *arr=value_array_new();
        for(long long i=start;i<end;i++) value_array_push(arr,a[0]->array.items[i]);
        return arr;
    }
    return value_retain(a[0]);
}
static Value *bi_flatten(Value **a, int n) {
    if(n<1||a[0]->type!=VAL_ARRAY)return n>0?value_retain(a[0]):value_array_new();
    Value *result=value_array_new();
    for(int i=0;i<a[0]->array.len;i++){
        Value *item=a[0]->array.items[i];
        if(item->type==VAL_ARRAY)
            for(int j=0;j<item->array.len;j++) value_array_push(result,item->array.items[j]);
        else
            value_array_push(result,item);
    }
    return result;
}
static Value *bi_range(Value **a, int n) {
    long long start=0,stop=0,step=1;
    if(n==1){stop=(a[0]->type==VAL_INT)?a[0]->int_val:(long long)a[0]->float_val;}
    else if(n>=2){
        start=(a[0]->type==VAL_INT)?a[0]->int_val:(long long)a[0]->float_val;
        stop=(a[1]->type==VAL_INT)?a[1]->int_val:(long long)a[1]->float_val;
        if(n>=3)step=(a[2]->type==VAL_INT)?a[2]->int_val:(long long)a[2]->float_val;
    }
    if(step==0)step=1;
    Value *arr=value_array_new();
    if(step>0){for(long long i=start;i<stop;i+=step){Value *v=value_int(i);value_array_push(arr,v);value_release(v);}}
    else      {for(long long i=start;i>stop;i+=step){Value *v=value_int(i);value_array_push(arr,v);value_release(v);}}
    return arr;
}
static Value *bi_zip(Value **a, int n) {
    if(n<2)return value_array_new();
    int minlen=-1;
    for(int i=0;i<n;i++){
        if(a[i]->type!=VAL_ARRAY)return value_array_new();
        if(minlen<0||a[i]->array.len<minlen)minlen=a[i]->array.len;
    }
    if(minlen<0)minlen=0;
    Value *result=value_array_new();
    for(int i=0;i<minlen;i++){
        Value *tuple=value_array_new();
        for(int j=0;j<n;j++) value_array_push(tuple,a[j]->array.items[i]);
        value_array_push(result,tuple); value_release(tuple);
    }
    return result;
}
static Value *bi_enumerate(Value **a, int n) {
    if(n<1||a[0]->type!=VAL_ARRAY)return value_array_new();
    long long start=(n>=2&&a[1]->type==VAL_INT)?a[1]->int_val:0;
    Value *result=value_array_new();
    for(int i=0;i<a[0]->array.len;i++){
        Value *pair=value_array_new();
        Value *idx=value_int(start+i);
        value_array_push(pair,idx); value_release(idx);
        value_array_push(pair,a[0]->array.items[i]);
        value_array_push(result,pair); value_release(pair);
    }
    return result;
}
static Value *bi_all(Value **a, int n) {
    if(n<1||a[0]->type!=VAL_ARRAY)return value_bool(1);
    for(int i=0;i<a[0]->array.len;i++)
        if(!value_truthy(a[0]->array.items[i]))return value_bool(0);
    return value_bool(1);
}
static Value *bi_any(Value **a, int n) {
    if(n<1||a[0]->type!=VAL_ARRAY)return value_bool(0);
    for(int i=0;i<a[0]->array.len;i++)
        if(value_truthy(a[0]->array.items[i]))return value_bool(1);
    return value_bool(0);
}
static Value *bi_unique(Value **a, int n) {
    if(n<1||a[0]->type!=VAL_ARRAY)return value_array_new();
    Value *result=value_array_new();
    for(int i=0;i<a[0]->array.len;i++){
        bool found=false;
        for(int j=0;j<result->array.len;j++)
            if(value_equals(result->array.items[j],a[0]->array.items[i])){found=true;break;}
        if(!found) value_array_push(result,a[0]->array.items[i]);
    }
    return result;
}
static Value *bi_compact(Value **a, int n) {
    if(n<1||a[0]->type!=VAL_ARRAY)return value_array_new();
    Value *result=value_array_new();
    for(int i=0;i<a[0]->array.len;i++)
        if(a[0]->array.items[i]->type!=VAL_NULL&&value_truthy(a[0]->array.items[i]))
            value_array_push(result,a[0]->array.items[i]);
    return result;
}
static Value *bi_first(Value **a, int n) {
    if(n<1)return value_null();
    if(a[0]->type==VAL_ARRAY) return a[0]->array.len>0?value_retain(a[0]->array.items[0]):value_null();
    if(a[0]->type==VAL_STRING) {
        if(a[0]->str_val[0]=='\0')return value_string("");
        char buf[2]={a[0]->str_val[0],'\0'}; return value_string(buf);
    }
    return value_null();
}
static Value *bi_last(Value **a, int n) {
    if(n<1)return value_null();
    if(a[0]->type==VAL_ARRAY){
        int len=a[0]->array.len;
        return len>0?value_retain(a[0]->array.items[len-1]):value_null();
    }
    if(a[0]->type==VAL_STRING){
        const char *s=a[0]->str_val; size_t len=strlen(s);
        if(len==0)return value_string("");
        char buf[2]={s[len-1],'\0'}; return value_string(buf);
    }
    return value_null();
}

/* ================================================================== dict functions */

static Value *bi_keys(Value **a, int n) {
    if(n<1||a[0]->type!=VAL_DICT)return value_array_new();
    Value *arr=value_array_new();
    for(int i=0;i<a[0]->dict.len;i++){
        Value *k=a[0]->dict.entries[i].key;
        if(k->type==VAL_STRING&&k->str_val[0]=='_'&&k->str_val[1]=='_')continue;
        value_array_push(arr,k);
    }
    return arr;
}
static Value *bi_values(Value **a, int n) {
    if(n<1||a[0]->type!=VAL_DICT)return value_array_new();
    Value *arr=value_array_new();
    for(int i=0;i<a[0]->dict.len;i++){
        Value *k=a[0]->dict.entries[i].key;
        if(k->type==VAL_STRING&&k->str_val[0]=='_'&&k->str_val[1]=='_')continue;
        value_array_push(arr,a[0]->dict.entries[i].val);
    }
    return arr;
}
static Value *bi_items(Value **a, int n) {
    if(n<1||a[0]->type!=VAL_DICT)return value_array_new();
    Value *arr=value_array_new();
    for(int i=0;i<a[0]->dict.len;i++){
        Value *k=a[0]->dict.entries[i].key;
        if(k->type==VAL_STRING&&k->str_val[0]=='_'&&k->str_val[1]=='_')continue;
        Value *pair=value_array_new();
        value_array_push(pair,k);
        value_array_push(pair,a[0]->dict.entries[i].val);
        value_array_push(arr,pair); value_release(pair);
    }
    return arr;
}
static Value *bi_has(Value **a, int n) {
    if(n<2)return value_bool(0);
    if(a[0]->type==VAL_DICT){Value *f=value_dict_get(a[0],a[1]);return value_bool(f?1:0);}
    if(a[0]->type==VAL_ARRAY){
        for(int i=0;i<a[0]->array.len;i++)
            if(value_equals(a[0]->array.items[i],a[1]))return value_bool(1);
        return value_bool(0);
    }
    if(a[0]->type==VAL_SET)return value_bool(value_set_has(a[0],a[1])?1:0);
    return value_bool(0);
}
static Value *bi_merge(Value **a, int n) {
    if(n<1||a[0]->type!=VAL_DICT)return n>0?value_retain(a[0]):value_dict_new();
    Value *result=value_dict_new();
    /* copy first dict */
    for(int i=0;i<a[0]->dict.len;i++)
        value_dict_set(result,a[0]->dict.entries[i].key,a[0]->dict.entries[i].val);
    /* merge remaining */
    for(int j=1;j<n;j++){
        if(a[j]->type!=VAL_DICT)continue;
        for(int i=0;i<a[j]->dict.len;i++)
            value_dict_set(result,a[j]->dict.entries[i].key,a[j]->dict.entries[i].val);
    }
    return result;
}
static Value *bi_delete(Value **a, int n) {
    if(n<2||a[0]->type!=VAL_DICT)return value_bool(0);
    /* Find and remove the key */
    for(int i=0;i<a[0]->dict.len;i++){
        if(value_equals(a[0]->dict.entries[i].key,a[1])){
            value_release(a[0]->dict.entries[i].key);
            value_release(a[0]->dict.entries[i].val);
            /* shift remaining entries */
            for(int j=i;j<a[0]->dict.len-1;j++)
                a[0]->dict.entries[j]=a[0]->dict.entries[j+1];
            a[0]->dict.len--;
            return value_bool(1);
        }
    }
    return value_bool(0);
}

/* ================================================================== utility */

static Value *bi_error(Value **a, int n) {
    const char *msg = (n > 0 && a[0]->type == VAL_STRING) ? a[0]->str_val : "error";
    do_throw(msg);
    return value_null();
}
static Value *bi_exit(Value **a, int n) {
    int code = (n > 0 && a[0]->type == VAL_INT) ? (int)a[0]->int_val : 0;
    exit(code);
    return value_null();
}
static Value *bi_hash(Value **a, int n) {
    if (n < 1) return value_int(0);
    if (a[0]->type == VAL_INT)    return value_int(a[0]->int_val);
    if (a[0]->type == VAL_STRING) {
        /* FNV-1a hash */
        unsigned long long h = 14695981039346656037ULL;
        for (const char *p = a[0]->str_val; *p; p++)
            h = (h ^ (unsigned char)*p) * 1099511628211ULL;
        return value_int((long long)h);
    }
    return value_int((long long)(uintptr_t)a[0]);
}
static Value *bi_copy_fn(Value **a, int n) {
    if (n < 1) return value_null();
    return value_copy(a[0]);
}
static Value *bi_id_fn(Value **a, int n) {
    if (n < 1) return value_int(0);
    return value_int((long long)(uintptr_t)a[0]);
}
static Value *bi_repr_fn(Value **a, int n) {
    if (n < 1) return value_string("null");
    char *s = value_to_string(a[0]); return value_string_take(s);
}

/* math constants as functions */
static Value *bi_math_pi(Value **a,int n){(void)a;(void)n;return value_float(3.14159265358979323846);}
static Value *bi_math_e(Value **a,int n){(void)a;(void)n;return value_float(2.71828182845904523536);}
static Value *bi_math_tau(Value **a,int n){(void)a;(void)n;return value_float(6.28318530717958647692);}
static Value *bi_math_inf(Value **a,int n){(void)a;(void)n;return value_float(1.0/0.0);}
static Value *bi_math_nan(Value **a,int n){(void)a;(void)n;return value_float(0.0/0.0);}

/* ================================================================== fs/os/net native builtins */

static Value *bi_read_file(Value **a, int n) {
    if (n < 1 || a[0]->type != VAL_STRING) return value_null();
    FILE *f = fopen(a[0]->str_val, "r");
    if (!f) return value_null();
    fseek(f, 0, SEEK_END);
    long sz = ftell(f); rewind(f);
    char *buf = malloc((size_t)sz + 1);
    size_t nr = fread(buf, 1, (size_t)sz, f);
    buf[nr] = '\0';
    fclose(f);
    return value_string_take(buf);
}

static Value *bi_write_file(Value **a, int n) {
    if (n < 2 || a[0]->type != VAL_STRING) return value_bool(0);
    FILE *f = fopen(a[0]->str_val, "w");
    if (!f) return value_bool(0);
    const char *content = (a[1]->type == VAL_STRING) ? a[1]->str_val : "";
    fputs(content, f);
    fclose(f);
    return value_bool(1);
}

static Value *bi_append_file(Value **a, int n) {
    if (n < 2 || a[0]->type != VAL_STRING) return value_bool(0);
    FILE *f = fopen(a[0]->str_val, "a");
    if (!f) return value_bool(0);
    const char *content = (a[1]->type == VAL_STRING) ? a[1]->str_val : "";
    fputs(content, f);
    fclose(f);
    return value_bool(1);
}

static Value *bi_file_exists(Value **a, int n) {
    if (n < 1 || a[0]->type != VAL_STRING) return value_bool(0);
    struct stat st;
    return value_bool(stat(a[0]->str_val, &st) == 0 ? 1 : 0);
}

static Value *bi_is_file(Value **a, int n) {
    if (n < 1 || a[0]->type != VAL_STRING) return value_bool(0);
    struct stat st;
    if (stat(a[0]->str_val, &st) != 0) return value_bool(0);
    return value_bool(S_ISREG(st.st_mode) ? 1 : 0);
}

static Value *bi_is_dir(Value **a, int n) {
    if (n < 1 || a[0]->type != VAL_STRING) return value_bool(0);
    struct stat st;
    if (stat(a[0]->str_val, &st) != 0) return value_bool(0);
    return value_bool(S_ISDIR(st.st_mode) ? 1 : 0);
}

static Value *bi_delete_file(Value **a, int n) {
    if (n < 1 || a[0]->type != VAL_STRING) return value_bool(0);
    return value_bool(remove(a[0]->str_val) == 0 ? 1 : 0);
}

static Value *bi_listdir(Value **a, int n) {
    const char *path = (n > 0 && a[0]->type == VAL_STRING) ? a[0]->str_val : ".";
    DIR *d = opendir(path);
    if (!d) return value_array_new();
    Value *arr = value_array_new();
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        Value *s = value_string(ent->d_name);
        value_array_push(arr, s);
        value_release(s);
    }
    closedir(d);
    return arr;
}

static Value *bi_getcwd(Value **a, int n) {
    (void)a; (void)n;
    char buf[4096];
    if (!getcwd(buf, sizeof(buf))) return value_string("");
    return value_string(buf);
}

static Value *bi_getenv(Value **a, int n) {
    if (n < 1 || a[0]->type != VAL_STRING) return value_string("");
    const char *val = getenv(a[0]->str_val);
    if (!val) {
        return (n > 1 && a[1]->type == VAL_STRING)
               ? value_string(a[1]->str_val)
               : value_string("");
    }
    return value_string(val);
}

static Value *bi_sleep_fn(Value **a, int n) {
    if (n < 1) return value_null();
    double secs = (a[0]->type == VAL_INT)   ? (double)a[0]->int_val :
                  (a[0]->type == VAL_FLOAT)  ? a[0]->float_val : 0.0;
    if (secs > 0) {
        struct timespec ts;
        ts.tv_sec  = (time_t)secs;
        ts.tv_nsec = (long)((secs - (double)ts.tv_sec) * 1e9);
        nanosleep(&ts, NULL);
    }
    return value_null();
}

static Value *bi_shell(Value **a, int n) {
    if (n < 1 || a[0]->type != VAL_STRING) return value_string("");
    /* run command, capture stdout */
    FILE *p = popen(a[0]->str_val, "r");
    if (!p) return value_string("");
    char *buf = NULL;
    size_t total = 0;
    char tmp[1024];
    size_t nr;
    while ((nr = fread(tmp, 1, sizeof(tmp), p)) > 0) {
        buf = realloc(buf, total + nr + 1);
        memcpy(buf + total, tmp, nr);
        total += nr;
        buf[total] = '\0';
    }
    pclose(p);
    if (!buf) return value_string("");
    /* strip trailing newline */
    while (total > 0 && (buf[total-1] == '\n' || buf[total-1] == '\r'))
        buf[--total] = '\0';
    return value_string_take(buf);
}

static Value *bi_file_size(Value **a, int n) {
    if (n < 1 || a[0]->type != VAL_STRING) return value_int(-1);
    struct stat st;
    if (stat(a[0]->str_val, &st) != 0) return value_int(-1);
    return value_int((long long)st.st_size);
}

static Value *bi_mkdir(Value **a, int n) {
    if (n < 1 || a[0]->type != VAL_STRING) return value_bool(0);
    return value_bool(mkdir(a[0]->str_val, 0755) == 0 ? 1 : 0);
}

/* ================================================================== registration */

void prism_register_stdlib(Env *env) {
    struct { const char *name; BuiltinFn fn; } tbl[] = {
        /* I/O */
        {"output",       bi_output},
        {"print",        bi_print},
        {"printf",       bi_printf_fn},
        {"input",        bi_input},
        /* type conversion */
        {"len",          bi_len},
        {"bool",         bi_bool_fn},
        {"int",          bi_int_fn},
        {"float",        bi_float_fn},
        {"str",          bi_str_fn},
        {"dict",         bi_dict_fn},
        {"set",          bi_set_fn},
        {"array",        bi_array_fn},
        {"tuple",        bi_tuple_fn},
        {"complex",      bi_complex_fn},
        {"type",         bi_type_fn},
        {"repr",         bi_repr_fn},
        {"copy",         bi_copy_fn},
        {"id",           bi_id_fn},
        /* assertions */
        {"assert",       bi_assert},
        {"assert_eq",    bi_assert_eq},
        /* math */
        {"abs",          bi_abs},
        {"sqrt",         bi_sqrt},
        {"floor",        bi_floor},
        {"ceil",         bi_ceil},
        {"round",        bi_round},
        {"pow",          bi_pow},
        {"sin",          bi_sin},
        {"cos",          bi_cos},
        {"tan",          bi_tan},
        {"asin",         bi_asin},
        {"acos",         bi_acos},
        {"atan",         bi_atan},
        {"atan2",        bi_atan2},
        {"log",          bi_log},
        {"log2",         bi_log2},
        {"log10",        bi_log10},
        {"exp",          bi_exp},
        {"hypot",        bi_hypot},
        {"isnan",        bi_isnan},
        {"isinf",        bi_isinf},
        {"min",          bi_min},
        {"max",          bi_max},
        {"clamp",        bi_clamp},
        {"sum",          bi_sum},
        /* string */
        {"chars",        bi_chars},
        {"upper",        bi_upper},
        {"lower",        bi_lower},
        {"trim",         bi_trim},
        {"ltrim",        bi_ltrim},
        {"rtrim",        bi_rtrim},
        {"starts",       bi_starts},
        {"startsWith",   bi_starts_with},
        {"ends",         bi_ends},
        {"endsWith",     bi_ends_with},
        {"contains",     bi_contains},
        {"floorDiv",     bi_floor_div},
        {"split",        bi_split},
        {"join",         bi_join},
        {"replace",      bi_replace},
        {"fromCharCode", bi_fromCharCode},
        {"chr",          bi_fromCharCode},
        {"ord",          bi_ord},
        {"parseInt",     bi_parseInt},
        {"parseFloat",   bi_parseFloat},
        {"hex",          bi_hex},
        {"bin",          bi_bin},
        {"oct",          bi_oct},
        {"repeat",       bi_repeat},
        {"padLeft",      bi_pad_left},
        {"padRight",     bi_pad_right},
        {"indexOf",      bi_index_of},
        {"countIn",      bi_count_in},
        /* array */
        {"push",         bi_push},
        {"pop",          bi_pop},
        {"sort",         bi_sort},
        {"sorted",       bi_sorted},
        {"reverse",      bi_reverse},
        {"slice",        bi_slice},
        {"flatten",      bi_flatten},
        {"range",        bi_range},
        {"zip",          bi_zip},
        {"enumerate",    bi_enumerate},
        {"all",          bi_all},
        {"any",          bi_any},
        {"unique",       bi_unique},
        {"compact",      bi_compact},
        {"first",        bi_first},
        {"last",         bi_last},
        /* dict */
        {"keys",         bi_keys},
        {"values",       bi_values},
        {"items",        bi_items},
        {"has",          bi_has},
        {"merge",        bi_merge},
        {"delete",       bi_delete},
        /* time */
        {"clock",        bi_clock},
        {"time",         bi_time_now},
        /* utility */
        {"error",        bi_error},
        {"exit",         bi_exit},
        {"hash",         bi_hash},
        /* fs/os/net builtins */
        {"read_file",    bi_read_file},
        {"write_file",   bi_write_file},
        {"append_file",  bi_append_file},
        {"file_exists",  bi_file_exists},
        {"is_file",      bi_is_file},
        {"is_dir",       bi_is_dir},
        {"delete_file",  bi_delete_file},
        {"listdir",      bi_listdir},
        {"getcwd",       bi_getcwd},
        {"getenv",       bi_getenv},
        {"sleep",        bi_sleep_fn},
        {"shell",        bi_shell},
        {"file_size",    bi_file_size},
        {"mkdir",        bi_mkdir},
        /* math constants as functions (for legacy compat) */
        {"PI",           bi_math_pi},
        {"E",            bi_math_e},
        {"TAU",          bi_math_tau},
        {"INF",          bi_math_inf},
        {"NAN",          bi_math_nan},
        {NULL, NULL}
    };
    for (int i = 0; tbl[i].name; i++) {
        Value *v = value_builtin(tbl[i].name, tbl[i].fn);
        env_set(env, tbl[i].name, v, false);
        value_release(v);
    }

    /* math constants as values */
    struct { const char *name; double val; } consts[] = {
        {"PI",  3.14159265358979323846},
        {"E",   2.71828182845904523536},
        {"TAU", 6.28318530717958647692},
        {NULL, 0.0}
    };
    for (int i = 0; consts[i].name; i++) {
        Value *v = value_float(consts[i].val);
        env_set(env, consts[i].name, v, true);
        value_release(v);
    }
    { Value *v = value_float(1.0/0.0); env_set(env,"INF",v,true); value_release(v); }
    { Value *v = value_float(0.0/0.0); env_set(env,"NAN",v,true); value_release(v); }
}
