/***************************************************************
 *
 * Copyright (C) 1990-2016, Condor Team, Computer Sciences Department,
 * University of Wisconsin-Madison, WI.
 * 
 * Licensed under the Apache License, Version 2.0 (the "License"); you
 * may not use this file except in compliance with the License.  You may
 * obtain a copy of the License at
 * 
 *    http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ***************************************************************/

#if !defined(_XFORM_UTILS_H)
#define _XFORM_UTILS_H

class MacroStreamXFormSource;
class XFormHash;

// in-place transform of a single ad 
// using the transform rules in xfm,
// variables live in the hashtable mset.
// returns < 0 on failure, errmsg will have details
//
int TransformClassAd (
	ClassAd * input_ad,           // the ad to be transformed
	MacroStreamXFormSource & xfm, // the set of transform rules
	XFormHash & mset,             // the hashtable used as temporary storage
	std::string & errmsg,
	unsigned int  flags=0);  // flags control output to stdout/stderr. 0x0001 = errors to stdout, 0x0002 = verbose logging to stdout.

/*
Basic workflow for classad transformation is 
 
init the macro set that the transform will use for temporary variables

  XFormHash mset;
  mset.init();

Load a transform source
  MacroStreamXFormSource xfm;
  xfm.load(FILE*, source)
or
  xfm.open(StringList & statements, source) (or XFormLoadFromJobRouterRoute() which calls this)
 
Save the hashtable state (so we can revert changes before each time we transform)
  chkpt = mset.save_state();

Transform a set of ads
  while (ads to transform) {
     // clean hashtable.
     mset.rewind_to_state(chkpt, false);
 
     // apply transforms
     TransformClassAd(ad, xfm, mset, errmsg, 0);
     TransformClassAd(ad, xfm2, mset, errmsg, 0);
  }

*/


// declare enough of the condor_params structure definitions so that we can define hashtable defaults
namespace condor_params {
	typedef struct string_value { char * psz; int flags; } string_value;
	struct key_value_pair { const char * key; const string_value * def; };
}


// The MacroStreamXFormSource holds a set of ClassAd transform commands
// 
class MacroStreamXFormSource : public MacroStreamCharSource
{
public:
	MacroStreamXFormSource(const char *nam=NULL);
	virtual ~MacroStreamXFormSource();
	// returns:
	//   < 0 = error, outmsg is error message
	//   > 0 = number of statements in MacroStreamCharSource, outmsg is TRANSFORM statement or empty
	// 
	int load(FILE* fp, MACRO_SOURCE & source);
	int open(StringList & statements, const MACRO_SOURCE & source);
	//bool open(const char * src_string, const MACRO_SOURCE & source); // this is the base class method...

	bool matches(ClassAd * candidate_ad);

	const char * getName() { return name.c_str(); }
	const char * setName(const char * nam) { name = nam; return getName(); }

	classad::ExprTree* getRequirements() { return requirements; }
	classad::ExprTree* setRequirements(const char * require);

	// these are used only when the source has an iterating TRANSFORM command
	bool has_pending_fp() { return fp_iter != NULL; }
	bool close_when_done(bool close) { close_fp_when_done = close; return has_pending_fp(); }
	int  parse_iterate_args(char * pargs, int expand_options, XFormHash &mset, std::string & errmsg);
	int will_iterate(XFormHash &mset, std::string & errmsg) {
		if (iterate_init_state <= 1) return iterate_init_state;
		return init_iterator(mset, errmsg);
	}
	bool iterate_init_pending() { return iterate_init_state > 1; }
	bool first_iteration(XFormHash &mset); // returns true if next_iteration should be called
	bool next_iteration(XFormHash &mset);  // returns true if there was a next
	void clear_iteration(XFormHash &mset); // clean up iteration variables in the hashtable
	void reset(XFormHash & mset); // reset the iterator variables

	MACRO_EVAL_CONTEXT_EX& context() { return ctx; }
	const char * getText() { return file_string.ptr(); } // return full (active) text of the transform, \n delimited.
	const char * getIterateArgs() { return iterate_args.ptr(); }
protected:
	std::string name;
	classad::ExprTree* requirements; // change to std::unique_ptr someday...
	MACRO_SET_CHECKPOINT_HDR * checkpoint;
	MACRO_EVAL_CONTEXT_EX ctx;

	// these are used when the transform iterates
	FILE* fp_iter; // when load stops at a TRANSFORM line, this holds the fp until parse_iterate_args is called
	int   fp_lineno;
	int   step;
	int   row;
	int   proc;
	bool  close_fp_when_done;
	char  iterate_init_state;
	SubmitForeachArgs oa;
	auto_free_ptr iterate_args; // copy of the arguments from the ITERATE line, set by load()
	auto_free_ptr curr_item; // so we can destructively edit the current item from the items list

	int set_iter_item(XFormHash &mset, const char* item);
	int init_iterator(XFormHash &mset, std::string & errmsg);
	//int report_empty_items(XFormHash& mset, std::string errmsg);
};


// The XFormHash class is used as scratch space and temporary variable storage
// for Classad transforms.
//
class XFormHash {
public:
	XFormHash();
	~XFormHash();

	void init();
	void clear(); // clear, but do not deallocate

	char * local_param(const char* name, const char* alt_name, MACRO_EVAL_CONTEXT & ctx);
	char * local_param(const char* name, MACRO_EVAL_CONTEXT & ctx) { return local_param(name, NULL, ctx); }
	char * expand_macro(const char* value, MACRO_EVAL_CONTEXT & ctx) { return ::expand_macro(value, LocalMacroSet, ctx); }
	const char * lookup(const char* name, MACRO_EVAL_CONTEXT & ctx) { return lookup_macro(name, LocalMacroSet, ctx); }

	void insert_source(const char * filename, MACRO_SOURCE & source);
	void set_RulesFile(const char * filename, MACRO_SOURCE & source);
	void set_local_param(const char* name, const char* value, MACRO_EVAL_CONTEXT & ctx);
	void set_arg_variable(const char* name, const char* value, MACRO_EVAL_CONTEXT & ctx);
	void set_local_param_used(const char* name);
	void setup_macro_defaults(); // setup live defaults table

	// stuff value into the submit's hashtable and mark 'name' as a used param
	// this function is intended for use during queue iteration to stuff changing values like $(Cluster) and $(Process)
	// Because of this the function does NOT make a copy of value, it's up to the caller to
	// make sure that value is not changed for the lifetime of possible macro substitution.
	void set_live_variable(const char* name, const char* live_value, MACRO_EVAL_CONTEXT & ctx);
	void set_iterate_step(int step, int proc);
	void set_iterate_row(int row, bool iterating);
	void clear_live_variables();

	const char * get_RulesFilename();
	MACRO_ITEM* lookup_exact(const char * name) { return find_macro_item(name, NULL, LocalMacroSet); }
	CondorError* error_stack() const {return LocalMacroSet.errors;}
	void warn_unused(FILE* out, const char *app);

	void dump(FILE* out, int flags);
	void push_error(FILE * fh, const char* format, ... ) CHECK_PRINTF_FORMAT(3,4);
	void push_warning(FILE * fh, const char* format, ... ) CHECK_PRINTF_FORMAT(3,4);

	MACRO_SET& macros() { return LocalMacroSet; }
	MACRO_SET_CHECKPOINT_HDR * save_state();
	bool rewind_to_state(MACRO_SET_CHECKPOINT_HDR * state, bool and_delete);

protected:
	MACRO_SET LocalMacroSet;

	// automatic 'live' submit variables. these pointers are set to point into the macro set allocation
	// pool. so the will be automatically freed. They are also set into the macro_set.defaults tables
	// so that whatever is set here will be found by $(Key) macro expansion.
	char* LiveProcessString;
	char* LiveRowString;
	char* LiveStepString;
	condor_params::string_value * LiveRulesFileMacroDef;
	condor_params::string_value * LiveIteratingMacroDef;

	// private helper functions
private:
	// disallow assignment and copy construction
	XFormHash(const XFormHash & that);
	XFormHash& operator=(const XFormHash & that);
};

// do one time global initialization for the XFormHash class
// (in future maybe re-init after reconfig)
const char * init_xform_default_macros();


// load a MacroStreamXFormSource from a jobrouter style route.
int XFormLoadFromJobRouterRoute (
	MacroStreamXFormSource & xform,
	std::string & routing_string,
	int & offset,
	const ClassAd & base_route_ad,
	int options);

// pass these options to XFormLoadFromJobRouterRoute to have it fixup some
// some brokenness in the default JobRouter route.
#define XForm_ConvertJobRouter_Remove_InputRSL 0x00001
#define XForm_ConvertJobRouter_Fix_EvalSet     0x00002

#endif // _XFORM_UTILS_H
