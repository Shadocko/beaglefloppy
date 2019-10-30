/**
 * beaglefloppy
 * Floppy preservation using a BeagleBone
 *
 * 2019 Emmanuel Raulo-Kumagai
 */

#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>

/// Path of beaglelogic block device used for capture
#define BEAGLELOGIC_DEV_PATH "/dev/beaglelogic"

/// Base config path in sysfs for beaglelogic capture device
#define BEAGLELOGIC_CONFIG_PATH "/sys/devices/virtual/misc/beaglelogic/"

/// Sample rate in Hz. 20MHz means 50ns per sample, which is double of what SuperCard Pro format uses.
/// Beaglelogic won't capture at 40MHz so 20MHz is the most convenient value.
#define SAMPLE_RATE 20000000

/// Number of SuperCard Pro time unit (e.g. 25ns) per beaglelogic-captured sample
#define TIME_UNITS_PER_SAMPLE 2

/// Size of capture in bytes (equates to number of data samples).
/// Experience shows that 16MiB of capture at 20MHz always lets us see 3 complete tracks
#define CAPTURE_SIZE 33554432

/// Stop track captures at this many revolutions
#define MAX_REVOLUTIONS 5

/// Mask of data bit in captured byte
#define DATA_MASK 1
/// Mask of index bit in captured byte
#define INDEX_MASK 2
/// Mask of unused bits in captured byte (should be 0 with unused inputs connected to ground)
#define UNUSED_MASK 0xfc

#define GPIO_DIR 31
#define GPIO_STEP 48
#define GPIO_SIDE 30
#define GPIO_TRACK0 60
#define GPIO_BASE_PATH "/sys/class/gpio/"

#define STRINGIFY_SUB(x) #x
#define STRINGIFY(x) STRINGIFY_SUB(x)

void print_usage( const char* argv0 )
{
    fprintf( stderr, "Usage:\n\t%s <file_prefix>\n", argv0 );
}

/**
 * Open file and write transmitted contents.
 */
void file_write(const char* path, const char* contents)
{
    FILE* out = fopen(path, "w");
    fputs(contents, out);
    fclose(out);
}

/**
 * Open file and read contents as integer.
 * @return read value or -1 upon error.
 */
int file_read_int(const char* path)
{
    FILE* in = fopen(path, "r");
    if(!in) return -1;
    int ret=-1;
    fscanf(in, "%d", &ret);
    fclose(in);
    return ret;
}

/**
 * Acquire one disk track.
 * @param[out] out output file handle
 * @param[in]  track_num  track number in range [0-82]
 * @param[in]  side       track side (0 or 1)
 */
void acquire_track(FILE *out, int track_num, int track_side)
{
    // Open capture device...
    FILE *in = fopen(BEAGLELOGIC_DEV_PATH, "r");
    
    int
        rd=0,
        total_rd=0,
        index_count=0,
        index_times[MAX_REVOLUTIONS+1],
        index_offsets[MAX_REVOLUTIONS+1],
        index_transitions[MAX_REVOLUTIONS+1],
        last_transition_time=-1,
        num_transitions=0,
        total_w=0,
        histo[16]={0};
    unsigned char buf[1024], prev_byte = 0xff;
    bool do_write=true; //false;

    // Retrieve position in file for track data header...
    fseek(out, 0, SEEK_END);
    long offset_track_header = ftell(out);
    
    // Add track information to file header...
    fseek( out, 16 + 4*(2*track_num+track_side), SEEK_SET );
    {
        uint8_t bytes[] = {
            offset_track_header&0xff,
            (offset_track_header>>8)&0xff,
            (offset_track_header>>16)&0xff,
            (offset_track_header>>24)&0xff
        };
        fwrite( bytes, 1, 4, out );
    }
    fseek(out, 0, SEEK_END);
    
    // Write empty track data header...
    {
        uint8_t bytes[64] = { 'T', 'R', 'K', 0 };
        bytes[3] = (uint8_t)(2*track_num + track_side);
        fwrite( bytes, 1, sizeof(bytes), out );
    }

    while( in && !feof(in) && (rd=fread(buf, 1, 1024, in))>0 && index_count<=MAX_REVOLUTIONS ) {
        for( int i=0 ; i<rd && index_count<=MAX_REVOLUTIONS ; ++i ) {
            if( (prev_byte & (INDEX_MASK|UNUSED_MASK)) == INDEX_MASK
                && (buf[i] & (INDEX_MASK|UNUSED_MASK)) == 0 )
            {
                // Index pulse found...
                printf( "index pulse found at read sample %d, write offset %d, transitons %d\n", total_rd+i, total_w, num_transitions );
                do_write = true;
                index_times[index_count] = (total_rd+i)*TIME_UNITS_PER_SAMPLE;
                index_transitions[index_count] = num_transitions;
                index_offsets[index_count++] = total_w;
                if( index_count>1 )
                    printf( "approx RPM: %f\n", 60.e9 / ((index_times[index_count-1]-index_times[index_count-2])*25.) );
            }
             if( (prev_byte & (DATA_MASK|UNUSED_MASK)) == DATA_MASK
                 && (buf[i] & (DATA_MASK|UNUSED_MASK)) == 0 )
            //if( ((prev_byte ^ buf[i]) & (DATA_MASK|UNUSED_MASK)) == DATA_MASK )
            {
                // Transition found...
                ++num_transitions;
                if( last_transition_time>=0 ) {
                    // Output cell duration in SCP time units...
                    int delta = (total_rd+i) * TIME_UNITS_PER_SAMPLE - last_transition_time;
                    int usecs = (delta*25 + 499)/1000;
                    ++histo[ usecs<15 ? usecs : 15 ];
                    if( do_write ) {
                        while( delta >= 0x10000 ) {
                            // Output zeroes in case of overflow...
                            uint8_t zeroes[] = {0, 0};
                            fwrite( zeroes, 1, 2, out );
                            total_w += 2;
                            delta -= 0x10000;
                        }
                        uint8_t out_bytes[2] = { (uint8_t)((delta>>8)&0xff), (uint8_t)(delta&0xff) };
                        fwrite( out_bytes, 1, 2, out );
                        total_w+=2;
                    }
                }
                last_transition_time = (total_rd+i) * TIME_UNITS_PER_SAMPLE;
            }
            prev_byte = buf[i];
        }
        total_rd += rd;
    }
acquire_track_end:
    fclose(in);
    
    // Print histogram...
    fprintf( stderr, "timing histogram:\n" );
    for( int i=0 ; i<15 ; ++i ) {
        fprintf(stderr, "~%dus %d\n", i+1, histo[i]);
    }
    fprintf(stderr, ">15us %d\n", histo[15]);
    
    // Add missing info to track data header...
    fseek( out, offset_track_header+4, SEEK_SET );
    for( int i=0 ; i+1<index_count ; ++i ) {
        uint8_t bytes[12] = {0};
        int duration = index_times[i+1] - index_times[i];
        bytes[0] = (uint8_t)(duration & 0xff);
        bytes[1] = (uint8_t)((duration>>8) & 0xff);
        bytes[2] = (uint8_t)((duration>>16) & 0xff);
        bytes[3] = (uint8_t)((duration>>24) & 0xff);
        int transitions = (index_transitions[i+1] - index_transitions[i]);
        bytes[4] = (uint8_t)(transitions & 0xff);
        bytes[5] = (uint8_t)((transitions>>8) & 0xff);
        bytes[6] = (uint8_t)((transitions>>16) & 0xff);
        bytes[7] = (uint8_t)((transitions>>24) & 0xff);
        int offset = index_offsets[i] + 4 + 12*MAX_REVOLUTIONS;
        bytes[8] = (uint8_t)(offset & 0xff);
        bytes[9] = (uint8_t)((offset>>8) & 0xff);
        bytes[10] = (uint8_t)((offset>>16) & 0xff);
        bytes[11] = (uint8_t)((offset>>24) & 0xff);
        fwrite(bytes, 1, 12, out);
    }
    
    fseek( out, 0, SEEK_END );
}

int main (int argc, char **argv)
{
    // Print usage if no output path provided...
    if( argc<2 ) {
        print_usage(argv[0]);
        return -2;
    }
    const char* filename = argv[1];
    
    // Open output file...
    FILE *out = fopen(filename, "w+");
    if( !out ) {
        fprintf( stderr, "Unable to open '%s' for writting.\n", filename );
        return -1;
    }
    
    // Write file header...
    {
        uint8_t bytes[680] = {
            'S', 'C', 'P', // SuperCard Pro file header
            0x09, // Version 0.9 of file format (?)
            0x04, // Commodore Amiga disk
            MAX_REVOLUTIONS,
            0, 0xa5, // First track is 0, last one is 165
            0x03, // Index mark used for queueing tracks, drive is 96TPI at 300RPM, no post-processing of flux data, no footer
            0x00, // Standard cell size (2 bytes)
            0x00, // Both heads contained in file
            0x00, // Standard time scale of 25ns
            0x00 // Checksum and track offsets to be computed afterwards
        };
        fwrite( bytes, 1, 680, out );
    }

    // Configure beaglelogic...
    file_write(BEAGLELOGIC_CONFIG_PATH "sampleunit", "1");
    file_write(BEAGLELOGIC_CONFIG_PATH "samplerate", STRINGIFY(SAMPLE_RATE));
    // Note: setting capture buffer size many times will sometimes fail and cause kernel oops...
    if( file_read_int(BEAGLELOGIC_CONFIG_PATH "memalloc") < CAPTURE_SIZE )
        file_write(BEAGLELOGIC_CONFIG_PATH "memalloc", STRINGIFY(CAPTURE_SIZE));
    
    // Configure GPIOs...
    file_write(GPIO_BASE_PATH "export", STRINGIFY(GPIO_DIR));
    file_write(GPIO_BASE_PATH "export", STRINGIFY(GPIO_STEP));
    file_write(GPIO_BASE_PATH "export", STRINGIFY(GPIO_SIDE));
    file_write(GPIO_BASE_PATH "export", STRINGIFY(GPIO_TRACK0));
    file_write(GPIO_BASE_PATH "gpio" STRINGIFY(GPIO_DIR) "/direction", "out");
    file_write(GPIO_BASE_PATH "gpio" STRINGIFY(GPIO_STEP) "/direction", "out");
    file_write(GPIO_BASE_PATH "gpio" STRINGIFY(GPIO_SIDE) "/direction", "out");
    file_write(GPIO_BASE_PATH "gpio" STRINGIFY(GPIO_TRACK0) "/direction", "in");
    file_write(GPIO_BASE_PATH "gpio" STRINGIFY(GPIO_DIR) "/value", "1");
    file_write(GPIO_BASE_PATH "gpio" STRINGIFY(GPIO_STEP) "/value", "0");
    file_write(GPIO_BASE_PATH "gpio" STRINGIFY(GPIO_SIDE) "/value", "0");

    usleep(500000);

    // Return to track 0...
    while( file_read_int(GPIO_BASE_PATH "gpio" STRINGIFY(GPIO_TRACK0) "/value")==1 ) {
        file_write(GPIO_BASE_PATH "gpio" STRINGIFY(GPIO_STEP) "/value", "1");
        usleep(25000);
        file_write(GPIO_BASE_PATH "gpio" STRINGIFY(GPIO_STEP) "/value", "0");
        usleep(25000);
    }
    file_write(GPIO_BASE_PATH "gpio" STRINGIFY(GPIO_DIR) "/value", "0");
    usleep(250000);

    // Acquire tracks...
    for( int track=0 ; track<83 ; ++track ) {
        if( track>0 ) {
            file_write(GPIO_BASE_PATH "gpio" STRINGIFY(GPIO_STEP) "/value", "1");
            usleep(250000);
            file_write(GPIO_BASE_PATH "gpio" STRINGIFY(GPIO_STEP) "/value", "0");
        }
        file_write(GPIO_BASE_PATH "gpio" STRINGIFY(GPIO_SIDE) "/value", "1");
        usleep(250000);

        printf( "Track %d side A...\n", track);
        acquire_track(out, track, 0);
        printf("Done.\n");
        
        file_write(GPIO_BASE_PATH "gpio" STRINGIFY(GPIO_SIDE) "/value", "0");
        usleep(250000);

        printf( "Track %d side B...\n", track);
        acquire_track(out, track, 1);
        printf("Done.\n");
    }

    // Compute file checksum...

    fclose(out);

    // file_write(GPIO_BASE_PATH "unexport", STRINGIFY(GPIO_DIR));
    // file_write(GPIO_BASE_PATH "unexport", STRINGIFY(GPIO_STEP));
    // file_write(GPIO_BASE_PATH "unexport", STRINGIFY(GPIO_SIDE));
    // file_write(GPIO_BASE_PATH "unexport", STRINGIFY(GPIO_TRACK0));

    return 0;
}
