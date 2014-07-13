#include<stdio.h>
#include<stdlib.h>
#include<pthread.h>
#include<time.h>
#include<sys/ipc.h>
#include<sys/shm.h>

#include "fpp.h"
#include "param.h"
#include "city.h"

// City hash of an unsigned number
#define LL long long

#define foreach(i, n) for(i = 0; i < n; i ++)

// Compute an expensive hash using multiple applications of cityhash
LL hash(LL key)
{
	uint32_t lo = (LL) CityHash32((char *) &key, 4);
	uint32_t hi = (LL) CityHash32((char *) &lo, 4);

	LL hi_LL = (LL) hi;
	return ((hi_LL << 32) | lo);
}

struct KV
{
	LL key;
	LL value;
};

struct KV *ht_log;

struct IDX_BKT
{
	LL slots[SLOTS_PER_BKT];
};
struct IDX_BKT *ht_index;

#define INVALID_KV_I ((LL) (HT_LOG_CAP + 1))

// The index into the KV log is stored modulo HT_LOG_CAP
#define SLOT_TO_LOG_I(s) (s >> 16)
#define SLOT_TO_TAG(s) ((int) (s & 0xffff))

#define HASH_TO_TAG(h) ((int) (h & 0xffff))
#define HASH_TO_BUCKET(h) ((int) ((h >> 16) & HT_INDEX_N_))

LL randLL();

// Each packet contains a random 64-bit number.
LL *pkts;

int sum = 0;
int succ = 0;
int fail_1 = 0;			// Tag matches but log entry doesn't
int fail_2 = 0;			// Pkt not found

// batch_index must be declared outside process_pkts_in_batch
int batch_index = 0;

#include "fpp.h"
void process_pkts_in_batch(LL *pkt_lo)
{
	LL key_hash[BATCH_SIZE];
	int key_tag[BATCH_SIZE];
	int ht_bucket[BATCH_SIZE];
	LL *slots[BATCH_SIZE];
	int found[BATCH_SIZE];
	int i[BATCH_SIZE];
	int log_i[BATCH_SIZE];

	int I = 0;			// batch index
	void *batch_rips[BATCH_SIZE];		// goto targets
	int iMask = 0;		// No packet is done yet

	int temp_index;
	for(temp_index = 0; temp_index < BATCH_SIZE; temp_index ++) {
		batch_rips[temp_index] = &&label_0;
	}

label_0:

        key_hash[I] = hash(pkt_lo[I]);
        
        key_tag[I] = HASH_TO_TAG(key_hash[I]);
        ht_bucket[I] = HASH_TO_BUCKET(key_hash[I]);
        
        FPP_PSS(&ht_index[ht_bucket[I]], label_1);
label_1:

        slots[I] = ht_index[ht_bucket[I]].slots;
        
        found[I] = 0;
        
        for(i[I] = 0; i[I] < SLOTS_PER_BKT; i[I] ++) {
            
            // Tag matched
            if(SLOT_TO_TAG(slots[I][i[I]]) == key_tag[I]) {
                log_i[I] = SLOT_TO_LOG_I(slots[I][i[I]]);
                FPP_PSS(&ht_log[log_i[I]], label_2);
label_2:

                // Log entry also matches
                if(ht_log[log_i[I]].key == pkt_lo[I]) {
                    found[I] = 1;
                    succ ++;
                    sum += (int) ht_log[log_i[I]].value;
                    break;
                } else {
                    fail_1 ++;
                }
            }
        }
        
        if(found[I] == 0) {
            fail_2 ++;
        }
    
end:
    batch_rips[I] = &&end;
    iMask = FPP_SET(iMask, I); 
    if(iMask == (1 << BATCH_SIZE) - 1) {
        return;
    }
    I = (I + 1) & BATCH_SIZE_;
    goto *batch_rips[I];

}



int main(int argc, char **argv)
{
	int i, j;
	long long log_i = 0;		// KV-level index of head of log

	// Hugepages for hash-index
	fprintf(stderr, "Size of hash index = %lu\n", HT_INDEX_N * sizeof(struct IDX_BKT));

	int sid = shmget(HT_INDEX_SID, HT_INDEX_N * sizeof(struct IDX_BKT), 
		IPC_CREAT | 0666 | SHM_HUGETLB);
	if(sid < 0) {
		fprintf(stderr, "Could not create MICA-style hash index\n");
		exit(-1);
	}
	ht_index = shmat(sid, 0, 0);

	// Mark all ht_index slots invalid
	for(i = 0; i < HT_INDEX_N; i ++) {
		LL *slots = ht_index[i].slots;
		for(j = 0; j < SLOTS_PER_BKT; j ++) {
			slots[j] = INVALID_KV_I << 16;
		}
	}

	// Hugepages for circular log
	fprintf(stderr, "Size of log = %lu\n", HT_LOG_CAP * sizeof(struct KV));

	sid = shmget(HT_LOG_SID, HT_LOG_CAP * sizeof(struct KV),
		IPC_CREAT | 0666 | SHM_HUGETLB);
	if(sid < 0) {
		fprintf(stderr, "Could not create MICA-style circular log\n");
		exit(-1);
	}
	ht_log = shmat(sid, 0, 0);
		
	// Allocate the packets and put them into the hash index
	printf("Putting packets into hash index\n");
	pkts = (LL *) malloc(NUM_PKTS * sizeof(LL));

	for(i = 0; i < NUM_PKTS; i++) {
		// Generate a new key-value pair, this will be put at log_i
		LL K = randLL();
		LL V = K + 1;
		
		LL key_hash = hash(K);	

		int key_tag = HASH_TO_TAG(key_hash);
		int ht_bucket = HASH_TO_BUCKET(key_hash);
	
		LL *slots = ht_index[ht_bucket].slots;
		for(j = 0; j < SLOTS_PER_BKT; j ++) {
			if(SLOT_TO_LOG_I(slots[j]) == INVALID_KV_I) {
				// Found an empty slot
				slots[j] = key_tag | ((log_i & HT_LOG_CAP_) << 16);
				break;
			}
		}
		
		if(j == SLOTS_PER_BKT) {
			// Did not find an empty slot, pick one slot at random
			int replace = rand() & SLOTS_PER_BKT_;
			slots[replace] = key_tag | ((log_i & HT_LOG_CAP_) << 16);
		}
	
		ht_log[log_i & HT_LOG_CAP_].key = K;
		ht_log[log_i & HT_LOG_CAP_].value = V;
		
		log_i ++;

		pkts[i] = K;
	}

	printf("Shuffling packets so that log accesses are random\n");
	for(i = 0; i < NUM_PKTS; i ++) {
		int j = rand() % (i + 1);
		LL temp = pkts[i];
		pkts[i] = pkts[j];
		pkts[j] = temp;
	}

	printf("Starting lookups\n");
	struct timespec start, end;
	clock_gettime(CLOCK_REALTIME, &start);

	for(i = 0; i < NUM_PKTS; i += BATCH_SIZE) {
		process_pkts_in_batch(&pkts[i]);
	}

	clock_gettime(CLOCK_REALTIME, &end);
	printf("Time = %f sum = %d, succ = %d, fail_1 = %d, fail_2 = %d\n", 
		(end.tv_sec - start.tv_sec) + (double) (end.tv_nsec - start.tv_nsec) / 1000000000,
		sum, succ, fail_1, fail_2);
}

LL randLL()
{
	LL rand1 = (LL) lrand48();
	LL rand2 = (LL) lrand48();
	return (rand1 << 32) ^ rand2;
}
