#include "extconf.h"

// This file exports functions used to access private Ruby VM APIs and internals.
// To do this, it imports a few VM internal (private) headers.
//
// **Important Note**: Our medium/long-term plan is to stop relying on all private Ruby headers, and instead request and
// contribute upstream changes so that they become official public VM APIs.
//
// In the meanwhile, be very careful when changing things here :)

#ifdef RUBY_MJIT_HEADER
  // Pick up internal structures from the private Ruby MJIT header file
  #include RUBY_MJIT_HEADER
#else
  // On older Rubies, use a copy of the VM internal headers shipped in the debase-ruby_core_source gem
  #include <vm_core.h>
#endif

#define PRIVATE_VM_API_ACCESS_SKIP_RUBY_INCLUDES
#include "private_vm_api_access.h"

// MRI has a similar rb_thread_ptr() function which we can't call it directly
// because Ruby does not expose the thread_data_type publicly.
// Instead, we have our own version of that function, and we lazily initialize the thread_data_type pointer
// from a known-correct object: the current thread.
//
// Note that beyond returning the rb_thread_struct*, rb_check_typeddata() raises an exception
// if the argument passed in is not actually a `Thread` instance.
static inline rb_thread_t *thread_struct_from_object(VALUE thread) {
  static const rb_data_type_t *thread_data_type = NULL;
  if (thread_data_type == NULL) thread_data_type = RTYPEDDATA_TYPE(rb_thread_current());

  return (rb_thread_t *) rb_check_typeddata(thread, thread_data_type);
}

rb_nativethread_id_t pthread_id_for(VALUE thread) {
  return thread_struct_from_object(thread)->thread_id;
}

// -----------------------------------------------------------------------------
// The sources below are modified versions of code extracted from the Ruby project.
// Each function is annotated with its origin, why we imported it, and the changes made.
//
// The Ruby project copyright and license follow:
// -----------------------------------------------------------------------------
// Copyright (C) 1993-2013 Yukihiro Matsumoto. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
// OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
// LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
// OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
// SUCH DAMAGE.

// Taken from upstream vm_core.h at commit 5f10bd634fb6ae8f74a4ea730176233b0ca96954 (March 2022, Ruby 3.2 trunk)
// Copyright (C) 2004-2007 Koichi Sasada
// to support our custom rb_profile_frames (see below)
// Modifications: None
#define ISEQ_BODY(iseq) ((iseq)->body)

// Taken from upstream vm_bactrace.c at commit 5f10bd634fb6ae8f74a4ea730176233b0ca96954 (March 2022, Ruby 3.2 trunk)
// Copyright (C) 1993-2012 Yukihiro Matsumoto
// to support our custom rb_profile_frames (see below)
// Modifications: None
inline static int
calc_pos(const rb_iseq_t *iseq, const VALUE *pc, int *lineno, int *node_id)
{
    VM_ASSERT(iseq);
    VM_ASSERT(ISEQ_BODY(iseq));
    VM_ASSERT(ISEQ_BODY(iseq)->iseq_encoded);
    VM_ASSERT(ISEQ_BODY(iseq)->iseq_size);
    if (! pc) {
        if (ISEQ_BODY(iseq)->type == ISEQ_TYPE_TOP) {
            VM_ASSERT(! ISEQ_BODY(iseq)->local_table);
            VM_ASSERT(! ISEQ_BODY(iseq)->local_table_size);
            return 0;
        }
        if (lineno) *lineno = FIX2INT(ISEQ_BODY(iseq)->location.first_lineno);
#ifdef USE_ISEQ_NODE_ID
        if (node_id) *node_id = -1;
#endif
        return 1;
    }
    else {
        ptrdiff_t n = pc - ISEQ_BODY(iseq)->iseq_encoded;
        VM_ASSERT(n <= ISEQ_BODY(iseq)->iseq_size);
        VM_ASSERT(n >= 0);
        ASSUME(n >= 0);
        size_t pos = n; /* no overflow */
        if (LIKELY(pos)) {
            /* use pos-1 because PC points next instruction at the beginning of instruction */
            pos--;
        }
#if VMDEBUG && defined(HAVE_BUILTIN___BUILTIN_TRAP)
        else {
            /* SDR() is not possible; that causes infinite loop. */
            rb_print_backtrace();
            __builtin_trap();
        }
#endif
        if (lineno) *lineno = rb_iseq_line_no(iseq, pos);
#ifdef USE_ISEQ_NODE_ID
        if (node_id) *node_id = rb_iseq_node_id(iseq, pos);
#endif
        return 1;
    }
}

// Taken from upstream vm_bactrace.c at commit 5f10bd634fb6ae8f74a4ea730176233b0ca96954 (March 2022, Ruby 3.2 trunk)
// Copyright (C) 1993-2012 Yukihiro Matsumoto
// to support our custom rb_profile_frames (see below)
// Modifications: None
inline static int
calc_lineno(const rb_iseq_t *iseq, const VALUE *pc)
{
    int lineno;
    if (calc_pos(iseq, pc, &lineno, NULL)) return lineno;
    return 0;
}

// Taken from upstream vm_bactrace.c at commit 5f10bd634fb6ae8f74a4ea730176233b0ca96954 (March 2022, Ruby 3.2 trunk)
// Copyright (C) 1993-2012 Yukihiro Matsumoto
// Modifications:
// * Renamed rb_profile_frames => ddtrace_rb_profile_frames
// * Add thread argument
// * Add is_ruby_frame argument
// * Removed `if (lines)` tests -- require/assume that like `buff`, `lines` is always specified
//
// What is rb_profile_frames?
// `rb_profile_frames` is a Ruby VM debug API added for use by profilers for sampling the stack trace of a Ruby thread.
// Its main other user is the stackprof profiler: https://github.com/tmm1/stackprof .
//
// Why do we need a custom version of rb_profile_frames?
//
// There are a few reasons:
// 1. To backport improved behavior to older Rubies. Prior to Ruby 3.0 (https://github.com/ruby/ruby/pull/3299),
//    rb_profile_frames skipped CFUNC frames, aka frames that are implemented with native code, and thus the resulting
//    stacks were quite incomplete as a big part of the Ruby standard library is implemented with native code.
//
// 2. To extend this function to work with any thread. The upstream rb_profile_frames function only targets the current
//    thread, and to support wall-clock profiling we require sampling other threads. This is only safe because of the
//    Global VM Lock. (We don't yet support sampling Ractors beyond the main one; we'll need to find a way to do it
//    safely first.)
//
// 3. To get more information out of the Ruby VM. The Ruby VM has a lot more information than is exposed through
//    rb_profile_frames, and by making our own copy of this function we can extract more of this information.
//    See for backtracie gem (https://github.com/ivoanjo/backtracie) for an exploration of what can potentially be done.
//
// 4. Because we haven't yet submitted patches to upstream Ruby. As with any changes on the `private_vm_api_access.c`,
//    our medium/long-term plan is to contribute upstream changes and make it so that we don't need any of this
//    on modern Rubies.
int ddtrace_rb_profile_frames(VALUE thread, int start, int limit, VALUE *buff, int *lines, bool* is_ruby_frame)
{
    int i;
    // Modified from upstream: Instead of using `GET_EC` to collect info from the current thread,
    // support sampling any thread (including the current) passed as an argument
    const rb_execution_context_t *ec = thread_struct_from_object(thread)->ec;
    const rb_control_frame_t *cfp = ec->cfp, *end_cfp = RUBY_VM_END_CONTROL_FRAME(ec);
    const rb_callable_method_entry_t *cme;

    for (i=0; i<limit && cfp != end_cfp;) {
        if (VM_FRAME_RUBYFRAME_P(cfp)) {
            if (start > 0) {
                start--;
                continue;
            }

            /* record frame info */
            cme = rb_vm_frame_method_entry(cfp);
            if (cme && cme->def->type == VM_METHOD_TYPE_ISEQ) {
                buff[i] = (VALUE)cme;
            }
            else {
                buff[i] = (VALUE)cfp->iseq;
            }

            lines[i] = calc_lineno(cfp->iseq, cfp->pc);
            is_ruby_frame[i] = true;
            i++;
        }
        else {
            cme = rb_vm_frame_method_entry(cfp);
            if (cme && cme->def->type == VM_METHOD_TYPE_CFUNC) {
                buff[i] = (VALUE)cme;
                lines[i] = 0;
                is_ruby_frame[i] = false;
                i++;
            }
        }
        cfp = RUBY_VM_PREVIOUS_CONTROL_FRAME(cfp);
    }

    return i;
}

#ifdef USE_BACKPORTED_RB_PROFILE_FRAME_METHOD_NAME

// Taken from upstream vm_bactrace.c at commit 5f10bd634fb6ae8f74a4ea730176233b0ca96954 (March 2022, Ruby 3.2 trunk)
// Copyright (C) 1993-2012 Yukihiro Matsumoto
// to support our custom rb_profile_frame_method_name (see below)
// Modifications: None
static VALUE
id2str(ID id)
{
    VALUE str = rb_id2str(id);
    if (!str) return Qnil;
    return str;
}
#define rb_id2str(id) id2str(id)

// Taken from upstream vm_bactrace.c at commit 5f10bd634fb6ae8f74a4ea730176233b0ca96954 (March 2022, Ruby 3.2 trunk)
// Copyright (C) 1993-2012 Yukihiro Matsumoto
// to support our custom rb_profile_frame_method_name (see below)
// Modifications: None
static const rb_iseq_t *
frame2iseq(VALUE frame)
{
    if (NIL_P(frame)) return NULL;

    if (RB_TYPE_P(frame, T_IMEMO)) {
    switch (imemo_type(frame)) {
      case imemo_iseq:
        return (const rb_iseq_t *)frame;
      case imemo_ment:
        {
        const rb_callable_method_entry_t *cme = (rb_callable_method_entry_t *)frame;
        switch (cme->def->type) {
          case VM_METHOD_TYPE_ISEQ:
            return cme->def->body.iseq.iseqptr;
          default:
            return NULL;
        }
        }
      default:
        break;
    }
    }
    rb_bug("frame2iseq: unreachable");
}

// Taken from upstream vm_bactrace.c at commit 5f10bd634fb6ae8f74a4ea730176233b0ca96954 (March 2022, Ruby 3.2 trunk)
// Copyright (C) 1993-2012 Yukihiro Matsumoto
// to support our custom rb_profile_frame_method_name (see below)
// Modifications: None
static const rb_callable_method_entry_t *
cframe(VALUE frame)
{
    if (NIL_P(frame)) return NULL;

    if (RB_TYPE_P(frame, T_IMEMO)) {
    switch (imemo_type(frame)) {
      case imemo_ment:
            {
        const rb_callable_method_entry_t *cme = (rb_callable_method_entry_t *)frame;
        switch (cme->def->type) {
          case VM_METHOD_TYPE_CFUNC:
            return cme;
          default:
            return NULL;
        }
            }
          default:
            return NULL;
        }
    }

    return NULL;
}

// Taken from upstream vm_bactrace.c at commit 5f10bd634fb6ae8f74a4ea730176233b0ca96954 (March 2022, Ruby 3.2 trunk)
// Copyright (C) 1993-2012 Yukihiro Matsumoto
//
// Ruby 3.0 finally added support for showing CFUNC frames (frames for methods written using native code)
// in stack traces gathered via `rb_profile_frames` (https://github.com/ruby/ruby/pull/3299).
// To access this information on older Rubies, beyond using our custom `ddtrace_rb_profile_frames` above, we also need
// to backport the Ruby 3.0+ version of `rb_profile_frame_method_name`.
//
// Modifications:
// * Renamed rb_profile_frame_method_name => ddtrace_rb_profile_frame_method_name
VALUE
ddtrace_rb_profile_frame_method_name(VALUE frame)
{
    const rb_callable_method_entry_t *cme = cframe(frame);
    if (cme) {
        ID mid = cme->def->original_id;
        return id2str(mid);
    }
    const rb_iseq_t *iseq = frame2iseq(frame);
    return iseq ? rb_iseq_method_name(iseq) : Qnil;
}

#endif // USE_BACKPORTED_RB_PROFILE_FRAME_METHOD_NAME
