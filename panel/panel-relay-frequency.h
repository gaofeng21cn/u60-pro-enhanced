#ifndef U60_RELAY_FREQUENCY_H
#define U60_RELAY_FREQUENCY_H
/* Shared scan, selection and connection policy. DFS is intentionally excluded. */
static int relay_frequency(int f){
 if(f>=2412&&f<=2472&&(f-2412)%5==0)return 1;
 return f==5180||f==5200||f==5220||f==5240||f==5745||f==5765||f==5785||f==5805||f==5825;
}
#endif
