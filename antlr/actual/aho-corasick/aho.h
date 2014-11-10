#include "ds_queue.h"

#define AHO_MAX_STATES (505 * 505)
#define AHO_ALPHA_SIZE 256
#define AHO_FAIL -1

struct aho_state {
	int G[AHO_ALPHA_SIZE];		/**< Goto function */
	int F;						/**< Failure function */
	struct ds_queue output;	/**< Output patterns at this state */
};

void aho_init(struct aho_state **dfa);
void aho_add_pattern(struct aho_state *dfa, char *pattern, int index);
void aho_build_ff(struct aho_state *dfa);
