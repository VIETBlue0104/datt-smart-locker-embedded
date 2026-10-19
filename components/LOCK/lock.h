#ifndef __LOCK_H
#define __LOCK_H

#define LOCK_PIN_1 GPIO_NUM_25  // Chân điều khiển khóa ngăn 0
#define LOCK_PIN_2 GPIO_NUM_26  // Chân điều khiển khóa ngăn 1

void lock_init(void) ;
void open_lock(int compartment_index) ;


#endif