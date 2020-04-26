// Configure ESP8266 to blink an LED after a client has connected
// to the ESP8266 (which is configured as AP). We use a timer to 
// blink the LED and a series of callbac functions to detect
// a connection event and initiate the timer.

// Compile, link and then convert to a bin using the Makefile:  
//      make clean 
//      make
// Then flash to device using the Makefile.  Device must be in program mode
// which means you have to press and hold RST, press and hold PROGRAM, release
// RST and finally release PROGRAM.  PROGRAM is nothing more than GPIO0 connected
// to ground via a switch and resistor.
//      make flash

// IMPORTANT: Remember that the ESP8266 NON-OS firmware does not contain an
// operating system. No OS means no task scheduler. This means we have to use
// some other means of putting our code on the SoC for execution. In this
// example we'll use a series of callback functions. The 1st callback will 
// be executed when the SoC has completed all its setup tasks. The 2nd callback
// will be executed when the SoC detects a WiFi event.

//
// Includes:
// credentials.h: this contains our defines for the SSID and password
// ets_sys.h: this includes a whole lot of types, structs, and other stuff
// osapi.h: this is where we get our memory, string, and timer functions 
// gpio.h: you guessed it ... GPIO functions such as gpio_init
// os_type.h: this includes our defines for signal, event, and timer types 
// user_interfaces.h: this gets us the flash maps and the System_Event_t struct
// c_types.h: Also gets us the flash maps and all kinds of other attributes

#include "credentials.h"
#include "ets_sys.h"
#include "osapi.h"
#include "gpio.h"
#include "os_type.h"
#include "user_config.h"
#include "user_interface.h"
#include "c_types.h"

// RF Pre-Init function ... according to SDK API reference this needs to be
// in user_main.c even though we aren't using it.  It can be used to set RF
// options. We aren't setting any options so just leave it empty.
void ICACHE_FLASH_ATTR user_rf_pre_init(void)
{
}

// RF calibrate sector function ... again ... SDK API says to add this to
// user_main.c.  It is used to set the RF calibration sector.  Note that the
// SDK API says that you have to add it to user_main.c but you don't ever have
// to call it; it will be called by the SDK itself.
uint32 ICACHE_FLASH_ATTR user_rf_cal_sector_set(void)
{
    enum flash_size_map size_map = system_get_flash_size_map();
    uint32 rf_cal_sec = 0;

    switch (size_map) {
        case FLASH_SIZE_4M_MAP_256_256:
            rf_cal_sec = 128 - 5;
            break;

        case FLASH_SIZE_8M_MAP_512_512:
            rf_cal_sec = 256 - 5;
            break;

        case FLASH_SIZE_16M_MAP_512_512:
        case FLASH_SIZE_16M_MAP_1024_1024:

            rf_cal_sec = 512 - 5;
            break;

        case FLASH_SIZE_32M_MAP_512_512:
        case FLASH_SIZE_32M_MAP_1024_1024:
            rf_cal_sec = 1024 - 5;
            break;

        case FLASH_SIZE_64M_MAP_1024_1024:
            rf_cal_sec = 2048 - 5;
            break;
        case FLASH_SIZE_128M_MAP_1024_1024:
            rf_cal_sec = 4096 - 5;
            break;
        default:
            rf_cal_sec = 0;
            break;
    }

    return rf_cal_sec;
}

// Declare the system init done callback function. This function will run
// once the system initialization is complete. Make it static so you can't 
// access it outside this source file.
LOCAL void init_done_callback(void);

// Declare the WiFi event handler callback function. This is the function
// that will execute when the SoC detects a WiFi event. We will evaluate
// the event and if it is a connection we will kick off our timer.
// Yes the System_Event_t type is mixed case it's defined that way in user_interface.h.
LOCAL void wifi_event_handler_callback(System_Event_t *event);

// Declare the timer function
LOCAL void timer_function (void);

// Create the software timer
LOCAL os_timer_t the_timer;

// Define the system init done callback function. Inside this function setup the WiFi.
// Why are we using a callback function for this??? Because it allows the SoC
// time to get everything setup!
// Once the WiFi setup is complete then register the WiFi event handler callback function.
LOCAL void init_done_callback (void) {

  char const *SSID = WIFI_SSID;
  char const *PASSWORD = WIFI_PASSWORD;

  // Get the current AP configuration
  struct softap_config config;
  wifi_softap_get_config(&config);

  // Don't forget that config.ssid needs to be cast to a pointer because SSID is
  // itself a pointer (look above where you defined it). Also notice how we have to
  // null the SSID and password pointers or you will have junk left over from the
  // previous run.  We use os_bzero to null these pointers (aka char arrays).
  // Remember to use the exact number of characters in WIFI_SSID for ssid_len and os_memcpy.
  // Otherwise you'll end up with an SSID like this: ESPWHATEVER\x00!
  config.ssid_len = 7;
  os_bzero(&config.ssid, 32);
  os_memcpy(&config.ssid, SSID, 7);
  os_bzero(&config.password, 64);
  os_memcpy(&config.password, PASSWORD, 10);
  config.authmode = AUTH_WPA2_PSK;
  wifi_softap_set_config(&config);

  // Now register the WiFi event handler callback function. The SoC will call this
  // function (wifi_event_handler_callback) when it detects a WiFi event. We've got
  // code in wifi_event_handler_callback) to check the type of event and if it is
  // a connection event then we will kick off the timer.
  wifi_set_event_handler_cb(wifi_event_handler_callback);

}

// Define the WiFi event handler callback f unction. This is where we'll wait for an event.
// Once we have the event we will evaluate and if it is a WiFi connection event, we will 
// setup our timer and arm the timer. It (the timer that is) will then flash 
// an LED once per second to indicate that a client is connected to our ESP AP. Don't forget to 
// use os_delay_us to give the SoC time to do other stuff!
// Yes ... the System_Event_t type is mixed case ... it's defined that way in user_interface.h.
LOCAL void ICACHE_FLASH_ATTR wifi_event_handler_callback(System_Event_t *event) {

  switch (event->event) {
    case EVENT_SOFTAPMODE_STACONNECTED:
      // Disarm the timer ... it takes a pointer so pass the address of the_timer
      os_timer_disarm(&the_timer);

      // Setup the timer ... the timer will call timer_function when it runs
      // Pass timer_function to the procedure by casting it as a pointer to a timer function
      // Our timer_function doesn't take any parameters so use NULL as the last argument
      os_timer_setfn(&the_timer, (os_timer_func_t *)timer_function, NULL);

      // Arm the timer ... 1000ms = 1 second and 1 means it repeats
      // Our timer will kick off every second and run the function bound to it in the above
      // statement (os_timer_setfn)
      os_timer_arm(&the_timer, 1000, 1);
      break;
  }

  os_delay_us(100);

}
 
// Define the timer function ... read the status of GPIO2 ... if it is HIGH set it
// to LOW and vice versa. Don't forget the os_delay_us to allow the SoC
// time to do other functions!
LOCAL void timer_function (void) {
  if (GPIO_REG_READ(GPIO_OUT_ADDRESS) & BIT2)
    gpio_output_set(0, BIT2, BIT2, 0);
  else
    gpio_output_set(BIT2, 0, BIT2, 0);
  os_delay_us(100);
}

// Entry function ... execution starts here.  Note the use of attribute
// ICACHE_FLASH_ATTR which directs the ESP to store the user code in flash
// instead of RAM.
// NOTE: Unlike a normal C main function, user_init does not execute sequentially.
// Instead, it is executed asynchronously. In other words, when user_init is run
// it returns immediately and the SoC goes on to do other things (like setup the 
// WiFi based on stored parameters). In fact, there is no guarantee that the SoC
// has completed it's setup tasks by the time you complete user_init! Since we depend
// on the WiFi to be completely ready to go before we start our detect connection task,
// let's setup a callback function with system_init_done_cb. When the SoC has completed
// all it's setup work, it will execute our callback function which in turn will 
// setup the WiFi

void ICACHE_FLASH_ATTR user_init (void) {
  
  // Initialize the GPIO sub-system
  gpio_init();  

  // Set GPIO2 to be GPIO2 ... yeah it sounds stupid to do this but you
  // don't know how GPIO2 was previously configured ... it could have been
  // configured for something completely different
  PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO2_U, FUNC_GPIO2); 
  
  // Set GPIO2 as output and set it to LOW
  gpio_output_set(0, BIT2, BIT2, 0);

  // And here is our system init done callback. Once the SoC has done its 
  // setup it will execute the function init_done_callback.
  system_init_done_cb(init_done_callback);

}
