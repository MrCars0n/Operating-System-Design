#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#define MAX_PRIMES 100000 // Adjustable as needed for the chosen range

int primes[MAX_PRIMES];      // Shared list of primes
int prime_count = 0;         // Count of primes found

// Mutex (mutual exclusion) is used to protect shared resources from being accessed or modified concurrently by multiple threads
pthread_mutex_t prime_mutex;

// Function to check if a number is prime
bool is_prime(int num)
{
    if (num < 2) { return false; }
    if (num == 2) { return true; } // Special case for 2 (smallest prime)
    if (num % 2 == 0) { return false; } // Eliminate even numbers > 2
    for (int i = 3; i * i <= num; i += 2)
    { 
        // Check odd divisors only
        if (num % i == 0) { return false; }
    }
    return true;
}

// Runner function executed by each thread
void *runner(void *param)
{
    int *range = (int *)param;

    for (int i = range[0]; i <= range[1]; i++)
    {
        if (is_prime(i))
        {
            pthread_mutex_lock(&prime_mutex);   // Lock the mutex
            if (prime_count < MAX_PRIMES)       // Prevent overflow
            {
                primes[prime_count++] = i;
            }
            pthread_mutex_unlock(&prime_mutex); // Unlock the mutex
        }
    }
    pthread_exit(0);
}

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        printf("Usage: %s <start> <end>\n", argv[0]);
        return -1;
    }

    int start = atoi(argv[1]);
    int end = atoi(argv[2]);

    if (start > end)
    {
        printf("Invalid range: start should be greater than or equal to the end.\n");
        return -1;
    }

    // Declare threads
    pthread_t thread1, thread2, thread3, thread4;
    int range1[2], range2[2], range3[2], range4[2];

    pthread_mutex_init(&prime_mutex, NULL); // Initialize mutex

    // Assign ranges for each thread
    int total_numbers = end - start + 1;
    int chunk = total_numbers / 4;

    // Thread 1: Handles the first quarter of the range
    range1[0] = start;
    range1[1] = start + chunk - 1;

    // Thread 2: Handles the second quarter of the range
    range2[0] = start + chunk;
    range2[1] = start + 2 * chunk - 1;

    // Thread 3: Handles the third quarter of the range
    range3[0] = start + 2 * chunk;
    range3[1] = start + 3 * chunk - 1;

    // Thread 4: Handles the last quarter of the range
    range4[0] = start + 3 * chunk;
    range4[1] = end;

    // Create threads
    pthread_create(&thread1, NULL, runner, (void *)range1);
    pthread_create(&thread2, NULL, runner, (void *)range2);
    pthread_create(&thread3, NULL, runner, (void *)range3);
    pthread_create(&thread4, NULL, runner, (void *)range4);

    // Join threads
    pthread_join(thread1, NULL);
    pthread_join(thread2, NULL);
    pthread_join(thread3, NULL);
    pthread_join(thread4, NULL);

    // Output results
    printf("Prime numbers: ");
    for (int i = 0; i < prime_count; i++)
    {
        printf("%d ", primes[i]);
    }
    printf("\n");

    pthread_mutex_destroy(&prime_mutex); // Destroy the mutex
    return 0;
}
