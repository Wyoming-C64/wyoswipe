/*
 *  create_pdf.c: Routines that create the PDF erasure certificate
 *
 *  Copyright PartialVolume <https://github.com/PartialVolume>.
 *
 *  This program is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free Software
 *  Foundation, version 2.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along with
 *  this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <stdint.h>
#include "stdarg.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "wyoswipe.h"
#include "context.h"
#include "create_pdf.h"
#include "PDFGen/pdfgen.h"
#include "version.h"
#include "method.h"
#include "embedded_images/wyosupport.h"
#include "embedded_images/tick_erased.jpg.h"
#include "embedded_images/redcross.h"
#include "embedded_images/nwipe_exclamation.jpg.h"
#include "logging.h"
#include "options.h"
#include "prng.h"
#include "hpa_dco.h"
#include "miscellaneous.h"
#include <libconfig.h>
#include "conf.h"

#define text_size_data 10
#define drive_info_col_A 145
#define drive_info_col_B 395
#define drive_details_col_A 145
#define drive_details_col_B 405

struct pdf_doc* pdf;
struct pdf_object* page;

char model_header[50] = ""; /* Model text in the header */
char serial_header[30] = ""; /* Serial number text in the header */
char barcode[100] = ""; /* Contents of the barcode, i.e model:serial */
char pdf_footer[MAX_PDF_FOOTER_TEXT_LENGTH];
float height;
float page_width;
int status_icon;

int pica( float picas )
{
    return picas * 12;
}

char *get_service_tag() {
    const char *fallback = "_______________________";
    const char *dmi_paths[] = {
        "/sys/class/dmi/id/product_serial",
        "/sys/class/dmi/id/board_serial",
        "/sys/class/dmi/id/chassis_serial",
        "/sys/class/dmi/id/product_uuid"
    };
    const int path_count = sizeof(dmi_paths) / sizeof(dmi_paths[0]);

    char buffer[128];

    for (int i = 0; i < path_count; i++) {
        FILE *fp = fopen(dmi_paths[i], "r");
        if (fp == NULL)
            continue;

        if (fgets(buffer, sizeof(buffer), fp) != NULL) {
            fclose(fp);

            // Strip newline
            size_t len = strlen(buffer);
            if (len > 0 && buffer[len - 1] == '\n')
                buffer[len - 1] = '\0';

            if (strlen(buffer) > 0) {
                if (i == 3) {  // product_uuid
                    size_t buflen = strlen(buffer);
                    const char *suffix = buflen >= 12 ? buffer + buflen - 12 : buffer;
                    char result[32];  // 13 + 12 + 1 = 26 max, so 32 is safe
                    snprintf(result, sizeof(result), "UUID ending: %.12s", suffix);
                    return strdup(result);
                } else {
                    return strdup(buffer);
                }
            }
        } else {
            fclose(fp);
        }
    }

    return strdup(fallback);
}


int create_pdf( nwipe_context_t* ptr )
{
    extern nwipe_prng_t nwipe_twister;
    extern nwipe_prng_t nwipe_isaac;
    extern nwipe_prng_t nwipe_isaac64;
    extern nwipe_prng_t nwipe_add_lagg_fibonacci_prng;
    extern nwipe_prng_t nwipe_xoroshiro256_prng;

    /* Used by libconfig functions to retrieve data from nwipe.conf defined in conf.c */
    extern config_t nwipe_cfg;
    extern char nwipe_config_file[];

    //    char pdf_footer[MAX_PDF_FOOTER_TEXT_LENGTH];
    nwipe_context_t* c;
    c = ptr;
    //    char model_header[50] = ""; /* Model text in the header */
    //    char serial_header[30] = ""; /* Serial number text in the header */
    char device_size[100] = ""; /* Device size in the form xMB (xxxx bytes) */
    //    char barcode[100] = ""; /* Contents of the barcode, i.e model:serial */
    char verify[20] = ""; /* Verify option text */
    char blank[15] = ""; /* blanking pass, none, zeros, ones */
    char rounds[50] = ""; /* rounds ASCII numeric */
    char prng_type[50] = ""; /* type of prng, twister, isaac, isaac64 */
    char start_time_text[50] = "";
    char end_time_text[50] = "";
    char bytes_erased[50] = "";
    char HPA_status_text[50] = "";
    char HPA_size_text[50] = "";
    char errors[50] = "";
    char throughput_txt[50] = "";
    char bytes_percent_str[7] = "";

    //    int status_icon;

    //    float height;
    //    float page_width;

    struct pdf_info info = { .creator = "Wyo Support",
                             .producer = "Wyo Support",
                             .title = "PDF Drive Erasure Certificate",
                             .author = "WyoSwipe",
                             .subject = "Drive Erasure Certificate",
                             .date = "Today" };

    /* A pointer to the system time struct. */
    struct tm* p;

    /* variables used by libconfig */
    config_setting_t* setting;
    const char *business_name, *business_address, *business_citystatepostal, *contact_name, *contact_phone, *op_tech_name, 
        *customer_name, *customer_address, *customer_citystatepostal, *customer_contact_name, *customer_contact_phone;

    /* ------------------ */
    /* Initialise Various */

    /* Used to display correct icon on page 2 */
    status_icon = 0;  // zero don't display icon, see header STATUS_ICON_..

    // nwipe_log( NWIPE_LOG_NOTICE, "Create the PDF Drive erasure certificate" );
    pdf = pdf_create( PDF_LETTER_WIDTH, PDF_LETTER_HEIGHT, &info );

    /* Create footer text string and append the version */
    snprintf( pdf_footer, sizeof( pdf_footer ), "Drive Erasure by WyoSWipe version %s", version_string );

    pdf_set_font( pdf, "Helvetica" );
    struct pdf_object* page_1 = pdf_append_page( pdf );

    /* Obtain page page_width */
    page_width = pdf_page_width( page_1 );

    /*********************************************************************
     * Create header and footer on page 1, with the exception of the green
     * tick/red icon which is set from the 'status' section below
     */
    
    // Barcode
    snprintf( barcode, sizeof( barcode ), "%s:%s", c->device_model, c->device_serial_no );
    pdf_add_barcode( pdf, NULL, PDF_BARCODE_128A, 100, pica(61), 400, pica(2), barcode, PDF_BLACK );
    
    pdf_set_font( pdf, "Helvetica-Bold" );
    // Drive Model
    snprintf( model_header, sizeof( model_header ), " %s: %s ", "Model", c->device_model );
    pdf_add_text_wrap( pdf, NULL, model_header, 14, 0, pica(59), PDF_BLACK, page_width, PDF_ALIGN_CENTER, &height );
    // Drive Serial No.
    snprintf( serial_header, sizeof( serial_header ), " %s: %s ", "S/N", c->device_serial_no );
    pdf_add_text_wrap( pdf, NULL, serial_header, 14, 0, pica(57.5), PDF_BLACK, page_width, PDF_ALIGN_CENTER, &height );
    
    pdf_set_font( pdf, "Helvetica" );
    pdf_add_text_wrap(
        pdf, NULL, "Drive Data Erasure Report", 18, 0, pica(55), PDF_BLACK, page_width, PDF_ALIGN_CENTER, &height );
    pdf_add_text_wrap(
        pdf, NULL, "Page 1 - Erasure Status", 14, 0, pica(53), PDF_BLACK, page_width, PDF_ALIGN_CENTER, &height );
    
    // Logo - Left side
    pdf_add_image_data( pdf, NULL, 45, pica(52), 114, 100, bin2c_wyosupport_jpg, 36710 );
    pdf_add_line( pdf, NULL, 50, pica(51), 550, pica(51), 2, PDF_BLACK );
    
    // Footer
    pdf_add_line( pdf, NULL, 50, pica(4.5), 550, pica(4.5), 2, PDF_BLACK );
    pdf_add_text_wrap( pdf, NULL, pdf_footer, 8, 0, pica(3), PDF_BLACK, page_width, PDF_ALIGN_CENTER, &height );
    
    /* ------------------------ */
    /* Organisation Information */
    pdf_add_text( pdf, NULL, "Services Provided By:", 12, 50, pica(49.5), PDF_BLUE );
    // pdf_add_text( pdf, NULL, "Business Name:", 12, 60, 610, PDF_GRAY );
    // pdf_add_text( pdf, NULL, "Business Address:", 12, 60, 590, PDF_GRAY );
    // pdf_add_text( pdf, NULL, "Contact Name:", 12, 60, 570, PDF_GRAY );
    // pdf_add_text( pdf, NULL, "Contact Phone:", 12, 300, 570, PDF_GRAY );

    /* Obtain organisational details from nwipe.conf - See conf.c */
    setting = config_lookup( &nwipe_cfg, "Organisation_Details" );
    if( setting != NULL )
    {
        pdf_set_font( pdf, "Helvetica-Bold" );
        if( config_setting_lookup_string( setting, "Business_Name", &business_name ) )
        {
            // pdf_add_text( pdf, NULL, business_name, text_size_data, 153, 610, PDF_BLACK );
            pdf_add_text( pdf, NULL, business_name, text_size_data, 60, pica(48), PDF_BLACK );
        }
        if( config_setting_lookup_string( setting, "Business_Address", &business_address ) )
        {
            // pdf_add_text( pdf, NULL, business_address, text_size_data, 165, 590, PDF_BLACK );
            pdf_add_text( pdf, NULL, business_address, text_size_data, 60, pica(47), PDF_BLACK );
        }
        if( config_setting_lookup_string( setting, "Business_CityStatePostal", &business_citystatepostal ) )
        {
            // pdf_add_text( pdf, NULL, business_address, text_size_data, 165, 590, PDF_BLACK );
            pdf_add_text( pdf, NULL, business_citystatepostal, text_size_data, 60, pica(46), PDF_BLACK );
        }
        if( config_setting_lookup_string( setting, "Contact_Name", &contact_name ) )
        {
            // pdf_add_text( pdf, NULL, contact_name, text_size_data, 145, 570, PDF_BLACK );
            pdf_add_text( pdf, NULL, contact_name, text_size_data, 60, pica(45), PDF_BLACK );
        }
        if( config_setting_lookup_string( setting, "Contact_Phone", &contact_phone ) )
        {
            // pdf_add_text( pdf, NULL, contact_phone, text_size_data, 390, 570, PDF_BLACK );
            pdf_add_text( pdf, NULL, contact_phone, text_size_data, 60, pica(44), PDF_BLACK );
        }
        pdf_set_font( pdf, "Helvetica" );
    }
    else
    {
        nwipe_log( NWIPE_LOG_ERROR, "Cannot locate group [Organisation_Details] in %s", nwipe_config_file );
    }

    /* -------------------- */
    /* Customer Information */
    
    pdf_add_text( pdf, NULL, "For Customer:", 12, 300, pica(49.5), PDF_BLUE );
    // pdf_add_text( pdf, NULL, "Name:", 12, 60, 510, PDF_GRAY );
    // pdf_add_text( pdf, NULL, "Address:", 12, 60, 490, PDF_GRAY );
    // pdf_add_text( pdf, NULL, "Contact Name:", 12, 60, 470, PDF_GRAY );
    // pdf_add_text( pdf, NULL, "Contact Phone:", 12, 300, 470, PDF_GRAY );

    /* Obtain current customer details from nwipe.conf - See conf.c */
    setting = config_lookup( &nwipe_cfg, "Selected_Customer" );
    if( setting != NULL )
    {
        pdf_set_font( pdf, "Helvetica-Bold" );
        if( config_setting_lookup_string( setting, "Customer_Name", &customer_name ) )
        {
            pdf_add_text( pdf, NULL, customer_name, text_size_data, 310, pica(48), PDF_BLACK );
        }
        if( config_setting_lookup_string( setting, "Customer_Address", &customer_address ) )
        {
            pdf_add_text( pdf, NULL, customer_address, text_size_data, 310, pica(47), PDF_BLACK );
        }
        if( config_setting_lookup_string( setting, "Customer_CityStatePostal", &customer_citystatepostal ) )
        {
            pdf_add_text( pdf, NULL, customer_citystatepostal, text_size_data, 310, pica(46), PDF_BLACK );
        }
        if( config_setting_lookup_string( setting, "Contact_Name", &customer_contact_name ) )
        {
            pdf_add_text( pdf, NULL, customer_contact_name, text_size_data, 310, pica(45), PDF_BLACK );
        }
        if( config_setting_lookup_string( setting, "Contact_Phone", &customer_contact_phone ) )
        {
            pdf_add_text( pdf, NULL, customer_contact_phone, text_size_data, 310, pica(44), PDF_BLACK );
        }
        pdf_set_font( pdf, "Helvetica" );
    }
    else
    {
        nwipe_log( NWIPE_LOG_ERROR, "Cannot locate group [Selected_Customer] in %s", nwipe_config_file );
    }

    pdf_add_line( pdf, NULL, 50, pica(43), 550, pica(43), 1, PDF_GRAY );

    /******************
     * Drive Information
     */
    pdf_add_text( pdf, NULL, "Drive Information", 12, 50, pica(41.5), PDF_BLUE );

    /************
     * Make/model
     */
    pdf_add_text( pdf, NULL, "Make/Model:", text_size_data, 60, pica(40), PDF_GRAY );
    pdf_set_font( pdf, "Helvetica-Bold" );
    pdf_add_text( pdf, NULL, c->device_model, text_size_data, drive_info_col_A, pica(40), PDF_BLACK );
    pdf_set_font( pdf, "Helvetica" );

    /************
     * Serial no.
     */
    pdf_add_text( pdf, NULL, "Serial:", text_size_data, 310, pica(40), PDF_GRAY );
    if( c->device_serial_no[0] == 0 )
    {
        snprintf( c->device_serial_no, sizeof( c->device_serial_no ), "Unknown" );
    }
    pdf_set_font( pdf, "Courier-Bold" );
    pdf_add_text( pdf, NULL, c->device_serial_no, text_size_data, drive_info_col_B, pica(40), PDF_BLACK );
    pdf_set_font( pdf, "Helvetica" );

    /******************************
     * Bus type, ATA, USB, NVME etc
     */
    pdf_add_text( pdf, NULL, "Bus:", text_size_data, 310, pica(39), PDF_GRAY );
    pdf_set_font( pdf, "Helvetica-Bold" );
    pdf_add_text( pdf, NULL, c->device_type_str, text_size_data, drive_info_col_B, pica(39), PDF_BLACK );
    pdf_set_font( pdf, "Helvetica" );

    /****************************************
     * Computer/Device Serial Number 
     * (Blank if no serial number found, 
     * allowing for Technician to write in.)
     */
    char *service_tag_number = get_service_tag();

    if (c->device_type_str && strcmp(c->device_type_str, "USB") == 0) {
        // If the bus is USB, then it is 99% likely that this drive came out
        // of a different computer, therefore the service tag ID doesn't apply.
        free(service_tag_number);
        service_tag_number = strdup("_______________________");
    }
    pdf_add_text( pdf, NULL, "Computer S/N:", text_size_data, 310, pica(38), PDF_GRAY );
    pdf_set_font( pdf, "Courier-Bold" );
    pdf_add_text( pdf, NULL, service_tag_number, text_size_data, drive_info_col_B, pica(38), PDF_BLACK );
    pdf_set_font( pdf, "Helvetica" );

    /*************************
     * Capacity (Size) of Drive
     */

    /* Size (Apparent) */
    pdf_add_text( pdf, NULL, "Size(Apparent): ", text_size_data, 60, pica(39), PDF_GRAY );
    pdf_set_font( pdf, "Helvetica-Bold" );
    snprintf( device_size, sizeof( device_size ), "%s, %lli bytes", c->device_size_text, c->device_size );
    if( ( c->device_size == c->Calculated_real_max_size_in_bytes ) || c->device_type == NWIPE_DEVICE_NVME
        || c->device_type == NWIPE_DEVICE_VIRT || c->HPA_status == HPA_NOT_APPLICABLE || c->HPA_status != HPA_UNKNOWN )
    {
        pdf_add_text( pdf, NULL, device_size, text_size_data, drive_info_col_A, pica(39), PDF_DARK_GREEN );
    }
    else
    {
        pdf_add_text( pdf, NULL, device_size, text_size_data, drive_info_col_A, pica(39), PDF_RED );
    }
    pdf_set_font( pdf, "Helvetica" );

    /* Size (Real) */
    pdf_add_text( pdf, NULL, "Size(Real):", text_size_data, 60, pica(38), PDF_GRAY );
    pdf_set_font( pdf, "Helvetica-Bold" );
    if( c->device_type == NWIPE_DEVICE_NVME || c->device_type == NWIPE_DEVICE_VIRT
        || c->HPA_status == HPA_NOT_APPLICABLE )
    {
        snprintf( device_size, sizeof( device_size ), "%s, %lli bytes", c->device_size_text, c->device_size );
        pdf_add_text( pdf, NULL, device_size, text_size_data, drive_info_col_A, pica(38), PDF_DARK_GREEN );
    }
    else
    {
        /* If the calculared real max size as determined from HPA/DCO and libata data is larger than
         * or equal to the apparent device size then display that value in green.
         */
        if( c->Calculated_real_max_size_in_bytes >= c->device_size )
        {
            /* displays the real max size of the Drive from the DCO displayed in Green */
            snprintf( device_size,
                      sizeof( device_size ),
                      "%s, %lli bytes",
                      c->Calculated_real_max_size_in_bytes_text,
                      c->Calculated_real_max_size_in_bytes );
            pdf_add_text( pdf, NULL, device_size, text_size_data, drive_info_col_A, pica(38), PDF_DARK_GREEN );
        }
        else
        {
            /* If there is no real max size either because the drive or adapter doesn't support it */
            if( c->HPA_status == HPA_UNKNOWN )
            {
                snprintf( device_size, sizeof( device_size ), "Unknown" );
                pdf_add_text( pdf, NULL, device_size, text_size_data, drive_info_col_A, pica(38), PDF_RED );
            }
            else
            {
                /* we are already here because c->DCO_reported_real_max_size < 1 so if HPA enabled then use the
                 * value we determine from whether HPA set, HPA real exist and if not assume libata's value*/
                if( c->HPA_status == HPA_ENABLED )
                {
                    snprintf( device_size,
                              sizeof( device_size ),
                              "%s, %lli bytes",
                              c->device_size_text,
                              c->Calculated_real_max_size_in_bytes );
                    pdf_add_text( pdf, NULL, device_size, text_size_data, drive_info_col_A, pica(38), PDF_DARK_GREEN );
                }
                else
                {
                    /* Sanity check, should never get here! */
                    snprintf( device_size, sizeof( device_size ), "Sanity: HPA_status = %i", c->HPA_status );
                    pdf_add_text( pdf, NULL, device_size, text_size_data, drive_info_col_A, pica(38), PDF_RED );
                }
            }
        }
    }

    pdf_set_font( pdf, "Helvetica" );
    pdf_add_line( pdf, NULL, 50, pica(37), 550, pica(37), 1, PDF_GRAY );

    /*************************
     * Drive Erasure Details
     */
    pdf_add_text( pdf, NULL, "Drive Erasure Details", 12, 50, pica(35.5), PDF_BLUE );

    /* start time */
    pdf_add_text( pdf, NULL, "Start time:", text_size_data, 60, pica(34), PDF_GRAY );
    p = localtime( &c->start_time );
    snprintf( start_time_text,
              sizeof( start_time_text ),
              "%i/%02i/%02i %02i:%02i:%02i",
              1900 + p->tm_year,
              1 + p->tm_mon,
              p->tm_mday,
              p->tm_hour,
              p->tm_min,
              p->tm_sec );
    pdf_set_font( pdf, "Helvetica-Bold" );
    pdf_add_text( pdf, NULL, start_time_text, text_size_data, drive_details_col_A, pica(34), PDF_BLACK );
    pdf_set_font( pdf, "Helvetica" );

    /* end time */
    pdf_add_text( pdf, NULL, "End time:", text_size_data, 310, pica(34), PDF_GRAY );
    p = localtime( &c->end_time );
    snprintf( end_time_text,
              sizeof( end_time_text ),
              "%i/%02i/%02i %02i:%02i:%02i",
              1900 + p->tm_year,
              1 + p->tm_mon,
              p->tm_mday,
              p->tm_hour,
              p->tm_min,
              p->tm_sec );
    pdf_set_font( pdf, "Helvetica-Bold" );
    pdf_add_text( pdf, NULL, end_time_text, text_size_data, drive_details_col_B, pica(34), PDF_BLACK );
    pdf_set_font( pdf, "Helvetica" );

    /* Duration */
    pdf_add_text( pdf, NULL, "Duration:", text_size_data, 310, pica(33), PDF_GRAY );
    pdf_set_font( pdf, "Helvetica-Bold" );
    pdf_add_text( pdf, NULL, c->duration_str, text_size_data, drive_details_col_B, pica(33), PDF_BLACK );
    pdf_set_font( pdf, "Helvetica" );

    /*******************
     * Status of erasure
     */
    pdf_set_font( pdf, "Helvetica-Bold" );
    pdf_add_text( pdf, NULL, "Status:", text_size_data, 310, pica(35.5), PDF_GRAY );

    if( !strcmp( c->wipe_status_txt, "ERASED" )
        && ( c->HPA_status == HPA_DISABLED || c->HPA_status == HPA_NOT_APPLICABLE || c->device_type == NWIPE_DEVICE_NVME
             || c->device_type == NWIPE_DEVICE_VIRT ) )
    {
        pdf_add_text( pdf, NULL, c->wipe_status_txt, text_size_data, drive_details_col_B, pica(35.5), PDF_DARK_GREEN );
        pdf_add_ellipse( pdf, NULL, drive_details_col_B+25, pica(35.5)+5, 45, 10, 2, PDF_DARK_GREEN, PDF_TRANSPARENT );

        /* Display the green tick icon in the header */
        pdf_add_image_data( pdf, NULL, 450, pica(51.5), 100, 100, bin2c_te_jpg, 54896 );
        status_icon = STATUS_ICON_GREEN_TICK;  // used later on page 2
    }
    else
    {
        if( !strcmp( c->wipe_status_txt, "ERASED" )
            && ( c->HPA_status == HPA_ENABLED || c->HPA_status == HPA_UNKNOWN ) )
        {
            pdf_add_ellipse( pdf, NULL, drive_details_col_B+20, pica(35.5)+3, 45, 10, 2, PDF_RED, PDF_BLACK );
            pdf_add_text( pdf, NULL, c->wipe_status_txt, text_size_data, drive_details_col_B, pica(35.5), PDF_YELLOW );
            pdf_add_text( pdf, NULL, "See Warning!", text_size_data, drive_details_col_B+75, pica(35.5), PDF_RED );

            /* Display the yellow exclamation icon in the header */
            pdf_add_image_data( pdf, NULL, 450, pica(51.5), 100, 100, bin2c_nwipe_exclamation_jpg, 65791 );
            status_icon = STATUS_ICON_YELLOW_EXCLAMATION;  // used later on page 2
        }
        else
        {
            if( !strcmp( c->wipe_status_txt, "FAILED" ) )
            {
                // text shifted left slightly in ellipse due to extra character
                pdf_add_text( pdf, NULL, c->wipe_status_txt, text_size_data, drive_details_col_B+10, pica(35.5), PDF_RED );

                // Display the red cross in the header
                pdf_add_image_data( pdf, NULL, 450, pica(51.5), 100, 100, bin2c_redcross_jpg, 60331 );
                status_icon = STATUS_ICON_RED_CROSS;  // used later on page 2
            }
            else
            {
                pdf_add_text( pdf, NULL, c->wipe_status_txt, text_size_data, drive_details_col_B, pica(35.5), PDF_RED );

                // Print the red cross
                pdf_add_image_data( pdf, NULL, 450, pica(51.5), 100, 100, bin2c_redcross_jpg, 60331 );
                status_icon = STATUS_ICON_RED_CROSS;  // used later on page 2
            }
            pdf_add_ellipse( pdf, NULL, drive_details_col_B+30, pica(35.5)+5, 45, 10, 2, PDF_RED, PDF_TRANSPARENT );
        }
    }
    pdf_set_font( pdf, "Helvetica" );

    /********
     * Method
     */
    pdf_add_text( pdf, NULL, "Method:", text_size_data, 60, pica(31), PDF_GRAY );
    pdf_set_font( pdf, "Helvetica-Bold" );
    pdf_add_text( pdf, NULL, nwipe_method_label( nwipe_options.method ), text_size_data, drive_details_col_A, pica(31), PDF_BLACK );
    pdf_set_font( pdf, "Helvetica" );

    /***********
     * prng type
     */
    pdf_add_text( pdf, NULL, "PRNG algorithm:", text_size_data, 310, pica(31), PDF_GRAY );
    if( nwipe_options.method == &nwipe_verify_one || nwipe_options.method == &nwipe_verify_zero
        || nwipe_options.method == &nwipe_zero || nwipe_options.method == &nwipe_one )
    {
        snprintf( prng_type, sizeof( prng_type ), "Not applicable to method" );
    }
    else
    {
        if( nwipe_options.prng == &nwipe_twister )
        {
            snprintf( prng_type, sizeof( prng_type ), "Twister" );
        }
        else
        {
            if( nwipe_options.prng == &nwipe_isaac )
            {
                snprintf( prng_type, sizeof( prng_type ), "Isaac" );
            }
            else
            {
                if( nwipe_options.prng == &nwipe_isaac64 )
                {
                    snprintf( prng_type, sizeof( prng_type ), "Isaac64" );
                }
                else
                {
                    if( nwipe_options.prng == &nwipe_add_lagg_fibonacci_prng )
                    {
                        snprintf( prng_type, sizeof( prng_type ), "Fibonacci" );
                    }
                    else
                    {
                        if( nwipe_options.prng == &nwipe_xoroshiro256_prng )
                        {
                            snprintf( prng_type, sizeof( prng_type ), "XORshiro256" );
                        }
                        else
                        {
                            snprintf( prng_type, sizeof( prng_type ), "Unknown" );
                        }
                    }
                }
            }
        }
    }
    pdf_set_font( pdf, "Helvetica-Bold" );
    pdf_add_text( pdf, NULL, prng_type, text_size_data, drive_details_col_B, pica(31), PDF_BLACK );
    pdf_set_font( pdf, "Helvetica" );

    /******************************************************
     * Final blanking pass if selected, none, zeros or ones
     */
    if( nwipe_options.noblank )
    {
        strcpy( blank, "None" );
    }
    else
    {
        strcpy( blank, "Write Zeros" );
        if ( nwipe_options.method == &nwipe_one ) 
        {
            strcpy( blank, "Write Ones" );
        }
    }
    pdf_add_text( pdf, NULL, "Final Pass:", text_size_data, 60, pica(30), PDF_GRAY );
    pdf_set_font( pdf, "Helvetica-Bold" );
    pdf_add_text( pdf, NULL, blank, text_size_data, drive_details_col_A, pica(30), PDF_BLACK );
    pdf_set_font( pdf, "Helvetica" );

    /* ***********************************************************************
     * Create suitable text based on the numeric value of type of verification
     */
    switch( nwipe_options.verify )
    {
        case NWIPE_VERIFY_NONE:
            strcpy( verify, "None" );
            break;

        case NWIPE_VERIFY_LAST:
            strcpy( verify, "Last Pass" );
            break;

        case NWIPE_VERIFY_ALL:
            strcpy( verify, "All Passes" );
            break;
    }
    pdf_add_text( pdf, NULL, "Verify Pass:", text_size_data, 310, pica(30), PDF_GRAY );
    pdf_set_font( pdf, "Helvetica-Bold" );
    pdf_add_text( pdf, NULL, verify, text_size_data, drive_details_col_B, pica(30), PDF_BLACK );
    pdf_set_font( pdf, "Helvetica" );

    /* ************
     * bytes erased
     */
    pdf_add_text( pdf, NULL, "Bytes Erased*:", text_size_data, 60, pica(29), PDF_GRAY );
    pdf_set_font( pdf, "Helvetica-Bold" );

    /* Bytes erased is not applicable when user only requested a verify */
    if( nwipe_options.method == &nwipe_verify_one || nwipe_options.method == &nwipe_verify_zero )
    {
        snprintf( bytes_erased, sizeof( bytes_erased ), "Not applicable to method" );
        pdf_add_text( pdf, NULL, bytes_erased, text_size_data, drive_details_col_A, pica(29), PDF_BLACK );
    }
    else
    {
        if( c->device_type == NWIPE_DEVICE_NVME || c->device_type == NWIPE_DEVICE_VIRT
            || c->HPA_status == HPA_NOT_APPLICABLE )
        {
            convert_double_to_string( bytes_percent_str,
                                      (double) ( (double) c->bytes_erased / (double) c->device_size ) * 100 );

            snprintf( bytes_erased, sizeof( bytes_erased ), "%lli, (%s%%)", c->bytes_erased, bytes_percent_str );

            if( c->bytes_erased == c->device_size )
            {
                pdf_add_text( pdf, NULL, bytes_erased, text_size_data, drive_details_col_A, pica(29), PDF_DARK_GREEN );
            }
            else
            {
                pdf_add_text( pdf, NULL, bytes_erased, text_size_data, drive_details_col_A, pica(29), PDF_RED );
            }
        }
        else
        {

            convert_double_to_string(
                bytes_percent_str,
                (double) ( (double) c->bytes_erased / (double) c->Calculated_real_max_size_in_bytes ) * 100 );

            snprintf( bytes_erased, sizeof( bytes_erased ), "%lli, (%s%%)", c->bytes_erased, bytes_percent_str );

            if( c->bytes_erased == c->Calculated_real_max_size_in_bytes )
            {
                pdf_add_text( pdf, NULL, bytes_erased, text_size_data, drive_details_col_A, pica(29), PDF_DARK_GREEN );
            }
            else
            {
                pdf_add_text( pdf, NULL, bytes_erased, text_size_data, drive_details_col_A, pica(29), PDF_RED );
            }
        }
    }
    pdf_set_font( pdf, "Helvetica" );

    /************************************************
     * Rounds - How many times the method is repeated
     */
    pdf_add_text( pdf, NULL, "Rounds Completed:", text_size_data, 310, pica(29), PDF_GRAY );
    pdf_set_font( pdf, "Helvetica-Bold" );
    if( !strcmp( c->wipe_status_txt, "ERASED" ) )
    {
        snprintf( rounds, sizeof( rounds ), "%i of %i", c->round_working, nwipe_options.rounds );
        pdf_add_text( pdf, NULL, rounds, text_size_data, drive_details_col_B, pica(29), PDF_DARK_GREEN );
    }
    else
    {
        snprintf( rounds, sizeof( rounds ), "%i of %i", c->round_working - 1, nwipe_options.rounds );
        pdf_add_text( pdf, NULL, rounds, text_size_data, drive_details_col_B, pica(29), PDF_RED );
    }
    pdf_set_font( pdf, "Helvetica" );

    /*******************
     * HPA, DCO - LABELS
     */
    pdf_add_text( pdf, NULL, "HPA/DCO:", text_size_data, 60, pica(28), PDF_GRAY );
    pdf_set_font( pdf, "Helvetica-Bold" );
    pdf_add_text( pdf, NULL, HPA_status_text, text_size_data, drive_details_col_A, pica(28), PDF_BLACK );
    pdf_set_font( pdf, "Helvetica" );
    pdf_add_text( pdf, NULL, "HPA/DCO Size:", text_size_data, 310, pica(28), PDF_GRAY );

    /*******************
     * Populate HPA size
     */

    pdf_set_font( pdf, "Helvetica-Bold" );
    if( c->HPA_status == HPA_ENABLED )
    {
        snprintf( HPA_size_text, sizeof( HPA_size_text ), "%lli sectors", c->HPA_sectors );
        pdf_add_text( pdf, NULL, HPA_size_text, text_size_data, drive_details_col_B, pica(28), PDF_RED );
    }
    else
    {
        if( c->HPA_status == HPA_DISABLED )
        {
            snprintf( HPA_size_text, sizeof( HPA_size_text ), "No hidden sectors" );
            pdf_add_text( pdf, NULL, HPA_size_text, text_size_data, drive_details_col_B, pica(28), PDF_DARK_GREEN );
        }
        else
        {
            if( c->HPA_status == HPA_NOT_APPLICABLE )
            {
                snprintf( HPA_size_text, sizeof( HPA_size_text ), "Not Applicable" );
                pdf_add_text( pdf, NULL, HPA_size_text, text_size_data, drive_details_col_B, pica(28), PDF_DARK_GREEN );
            }
            else
            {
                if( c->HPA_status == HPA_UNKNOWN )
                {
                    snprintf( HPA_size_text, sizeof( HPA_size_text ), "Unknown" );
                    pdf_add_text( pdf, NULL, HPA_size_text, text_size_data, drive_details_col_B, pica(28), PDF_RED );
                }
            }
        }
    }

    pdf_set_font( pdf, "Helvetica" );

    /*********************
     * Populate HPA status (and size if not applicable, NVMe and VIRT)
     */
    if( c->device_type == NWIPE_DEVICE_NVME || c->device_type == NWIPE_DEVICE_VIRT
        || c->HPA_status == HPA_NOT_APPLICABLE )
    {
        snprintf( HPA_status_text, sizeof( HPA_status_text ), "Not applicable" );
        pdf_set_font( pdf, "Helvetica-Bold" );
        pdf_add_text( pdf, NULL, HPA_status_text, text_size_data, drive_details_col_A, pica(28), PDF_DARK_GREEN );
        pdf_set_font( pdf, "Helvetica" );
    }
    else
    {
        if( c->HPA_status == HPA_ENABLED )
        {
            snprintf( HPA_status_text, sizeof( HPA_status_text ), "Hidden sectors found!" );
            pdf_set_font( pdf, "Helvetica-Bold" );
            pdf_add_text( pdf, NULL, HPA_status_text, text_size_data, drive_details_col_A, pica(28), PDF_RED );
            pdf_set_font( pdf, "Helvetica" );
        }
        else
        {
            if( c->HPA_status == HPA_DISABLED )
            {
                snprintf( HPA_status_text, sizeof( HPA_status_text ), "No hidden sectors" );
                pdf_set_font( pdf, "Helvetica-Bold" );
                pdf_add_text( pdf, NULL, HPA_status_text, text_size_data, drive_details_col_A, pica(28), PDF_DARK_GREEN );
                pdf_set_font( pdf, "Helvetica" );
            }
            else
            {
                if( c->HPA_status == HPA_UNKNOWN )
                {
                    snprintf( HPA_status_text, sizeof( HPA_status_text ), "Unknown" );
                    pdf_set_font( pdf, "Helvetica-Bold" );
                    pdf_add_text( pdf, NULL, HPA_status_text, text_size_data, drive_details_col_A, pica(28), PDF_RED );
                    pdf_set_font( pdf, "Helvetica" );
                }
                else
                {
                    if( c->HPA_status == HPA_NOT_SUPPORTED_BY_DRIVE )
                    {
                        snprintf( HPA_status_text, sizeof( HPA_status_text ), "No hidden sectors, DDNSHDA**" );
                        pdf_set_font( pdf, "Helvetica-Bold" );
                        pdf_add_text( pdf, NULL, HPA_status_text, text_size_data, drive_details_col_A, pica(28), PDF_DARK_GREEN );
                        pdf_set_font( pdf, "Helvetica" );
                    }
                }
            }
        }
    }

    /************
     * Throughput
     */
    pdf_add_text( pdf, NULL, "Throughput:", text_size_data, 310, pica(27), PDF_GRAY );
    snprintf( throughput_txt, sizeof( throughput_txt ), "%s/sec", c->throughput_txt );
    pdf_set_font( pdf, "Helvetica-Bold" );
    pdf_add_text( pdf, NULL, throughput_txt, text_size_data, drive_details_col_B, pica(27), PDF_BLACK );
    pdf_set_font( pdf, "Helvetica" );

    /********
     * Errors
     */
    pdf_add_text( pdf, NULL, "Errors:", text_size_data, 60, pica(27), PDF_GRAY );
    pdf_set_font( pdf, "Helvetica-Bold" );
    snprintf( errors, sizeof( errors ), "Pass: %llu Sync: %llu Verify: %llu", c->pass_errors, c->fsyncdata_errors, c->verify_errors );
    if( c->pass_errors != 0 || c->fsyncdata_errors != 0 || c->verify_errors != 0 )
    {
        pdf_add_text( pdf, NULL, errors, text_size_data, drive_details_col_A, pica(27), PDF_RED );
    }
    else
    {
        pdf_add_text( pdf, NULL, errors, text_size_data, drive_details_col_A, pica(27), PDF_DARK_GREEN );
    }
    pdf_set_font( pdf, "Helvetica" );

    /*************
     * Information
     */
    pdf_add_text( pdf, NULL, "Information:", text_size_data, 60, pica(25.5), PDF_GRAY );
    pdf_set_font( pdf, "Helvetica-Bold" );

    if( !strcmp( c->wipe_status_txt, "ERASED" ) && c->HPA_status == HPA_ENABLED )
    {
        pdf_add_ellipse( pdf, NULL, drive_details_col_A+20, pica(25.5)+3, 30, 9, 2, PDF_RED, PDF_BLACK );
        pdf_add_text( pdf, NULL, "Warning", text_size_data, drive_details_col_A, pica(25.5), PDF_YELLOW );

        pdf_add_text( pdf,
                      NULL,
                      "Visible sectors erased as requested, however hidden sectors NOT erased.",
                      text_size_data,
                      drive_details_col_A+60,
                      pica(25.5),
                      PDF_RED );
    }
    else
    {
        if( c->HPA_status == HPA_UNKNOWN )
        {
            pdf_add_ellipse( pdf, NULL, drive_details_col_A+20, pica(25.5)+3, 30, 9, 2, PDF_RED, PDF_BLACK );
            pdf_add_text( pdf, NULL, "Warning", text_size_data, drive_details_col_A, pica(25.5), PDF_YELLOW );

            pdf_add_text( pdf,
                          NULL,
                          "HPA/DCO data unavailable, can not determine hidden sector status.",
                          text_size_data,
                          drive_details_col_A+60,
                          pica(25.5),
                          PDF_RED );
        }
    }
    pdf_set_font( pdf, "Helvetica" );
    /* info descripting what bytes erased actually means */
    pdf_add_text( pdf,
                  NULL,
                  "* Bytes erased: The amount of drive that's been erased at least once.",
                  8,
                  50,
                  pica(24),
                  PDF_BLACK );

    /* meaning of abreviation DDNSHPA */
    if( c->HPA_status == HPA_NOT_SUPPORTED_BY_DRIVE )
    {
        pdf_add_text(
            pdf, NULL, "** DDNSHPA = Drive does not support HPA/DCO.", 8, 50, pica(23), PDF_DARK_GREEN );
    }
    pdf_set_font( pdf, "Helvetica" );
    pdf_add_line( pdf, NULL, 50, pica(22.5), 550, pica(22.5), 1, PDF_GRAY );

    /************************
     * Flexible space between pica 8.5 and pica 22 used for Technician Notes
     */
    pdf_add_text( pdf, NULL, "Technician Notes", 12, 50, pica(21), PDF_BLUE );
    // pdf_add_rectangle( pdf, NULL, 50, pica(9), 500, pica(11.5), 0.5, PDF_BLACK);   
    pdf_add_line( pdf, NULL, 50, pica(8.5), 550, pica(8.5), 1, PDF_GRAY );
    
    /************************
     * Technician/Operator ID
     */
    pdf_add_text( pdf, NULL, "Technician/Operator ID", 12, 50, pica(7), PDF_BLUE );
    pdf_add_text( pdf, NULL, "Name/ID:", text_size_data, 60, pica(5.5), PDF_GRAY );
    pdf_add_text( pdf, NULL, "Signature:", 12, 300, pica(7), PDF_BLUE );
    pdf_add_line( pdf, NULL, 360, pica(5.5), 550, pica(5.5), 0.75, PDF_BLACK );

    pdf_set_font( pdf, "Helvetica-Bold" );
    /* Obtain organisational details from nwipe.conf - See conf.c */
    setting = config_lookup( &nwipe_cfg, "Organisation_Details" );
    if( config_setting_lookup_string( setting, "Op_Tech_Name", &op_tech_name ) )
    {
        pdf_add_text( pdf, NULL, op_tech_name, text_size_data, 120, pica(5.5), PDF_BLACK );
    }
    pdf_set_font( pdf, "Helvetica" );

    /***************************************
     * Populate page 2 and 3 with smart data
     */
    nwipe_get_smart_data( c );

    /*****************************
     * Create the reports filename
     *
     * Sanitize the strings that we are going to use to create the report filename
     * by converting any non alphanumeric characters to an underscore or hyphon
     */
    replace_non_alphanumeric( end_time_text, '-' );
    replace_non_alphanumeric( c->device_model, '_' );
    replace_non_alphanumeric( c->device_serial_no, '_' );
    snprintf( c->PDF_filename,
              sizeof( c->PDF_filename ),
              "%s/WyoSWipe-Report_%s_Model_%s_Serial_%s.pdf",
              nwipe_options.PDFreportpath,
              c->device_model,
              c->device_serial_no,
              end_time_text );

    pdf_save( pdf, c->PDF_filename );
    
    // Cleanup goes here!
    pdf_destroy( pdf );
    free(service_tag_number);
    return 0;
}

int nwipe_get_smart_data( nwipe_context_t* c )
{
    FILE* fp;

    char* pdata;
    char page_title[50];

    char smartctl_command[] = "smartctl -a %s";
    char smartctl_command2[] = "/sbin/smartctl -a %s";
    char smartctl_command3[] = "/usr/bin/smartctl -a %s";
    char final_cmd_smartctl[sizeof( smartctl_command3 ) + 256];
    char result[512];
    char smartctl_labels_to_anonymize[][18] = {
        "serial number:", "lu wwn device id:", "logical unit id:", "" /* Don't remove this empty string !, important */
    };

    int idx, idx2, idx3;
    int x, y;
    int set_return_value;
    int page_number;

    final_cmd_smartctl[0] = 0;

    /* Determine whether we can access smartctl, required if the PATH environment is not setup ! (Debian sid 'su' as
     * opposed to 'su -' */
    if( system( "which smartctl > /dev/null 2>&1" ) )
    {
        if( system( "which /sbin/smartctl > /dev/null 2>&1" ) )
        {
            if( system( "which /usr/bin/smartctl > /dev/null 2>&1" ) )
            {
                nwipe_log( NWIPE_LOG_WARNING, "Command not found. Install smartmontools !" );
            }
            else
            {
                sprintf( final_cmd_smartctl, smartctl_command3, c->device_name );
            }
        }
        else
        {
            sprintf( final_cmd_smartctl, smartctl_command2, c->device_name );
        }
    }
    else
    {
        sprintf( final_cmd_smartctl, smartctl_command, c->device_name );
    }

    if( final_cmd_smartctl[0] != 0 )
    {
        fp = popen( final_cmd_smartctl, "r" );

        if( fp == NULL )
        {
            nwipe_log( NWIPE_LOG_WARNING, "nwipe_get_smart_data(): Failed to create stream to %s", smartctl_command );

            set_return_value = 3;
        }
        else
        {
            x = 50;  // left side of page
            y = pica(49.5);  // top row of page
            page_number = 2;

            /* Create Page 2 of the report. This shows the drives smart data
             */
            page = pdf_append_page( pdf );

            /* Create the header and footer for page 2, the start of the smart data */
            snprintf( page_title, sizeof( page_title ), "Page %i - SMART Data", page_number );
            create_header_and_footer( c, page_title );

            /* Read the output a line at a time - output it. */
            while( fgets( result, sizeof( result ) - 1, fp ) != NULL )
            {
                /* Convert the label, i.e everything before the ':' to lower case, it's required to
                 * convert to lower case as smartctl seems to use inconsistent case when labeling
                 * for serial number, i.e mostly it produces labels "Serial Number:" but occasionally
                 * it produces a label "Serial number:" */

                idx = 0;

                while( result[idx] != 0 && result[idx] != ':' )
                {
                    /* If upper case alpha character, change to lower case */
                    if( result[idx] >= 'A' && result[idx] <= 'Z' )
                    {
                        result[idx] += 32;
                    }
                    idx++;
                }

                if( nwipe_options.quiet == 1 )
                {
                    for( idx2 = 0; idx2 < 3; idx2++ )
                    {
                        if( strstr( result, &smartctl_labels_to_anonymize[idx2][0] ) )
                        {
                            if( ( pdata = strstr( result, ":" ) ) )
                            {
                                idx3 = 1;
                                while( pdata[idx3] != 0 )
                                {
                                    if( pdata[idx3] != ' ' )
                                    {
                                        pdata[idx3] = 'X';
                                    }
                                    idx3++;
                                }
                            }
                        }
                    }
                }

                pdf_set_font( pdf, "Courier" );
                pdf_add_text( pdf, NULL, result, 8, x, y, PDF_BLACK );
                y -= 9;

                /* Have we reached the bottom of the page yet */
                if( y < pica(5.5) )
                {
                    /* Append an extra page */
                    page = pdf_append_page( pdf );
                    page_number++;
                    y = pica(49.5);

                    /* create the header and footer for the next page */
                    snprintf( page_title, sizeof( page_title ), "Page %i - SMART Data", page_number );
                    create_header_and_footer( c, page_title );
                }
            }
            set_return_value = 0;
        }
    }
    else
    {
        set_return_value = 1;
    }
    return set_return_value;
}

void create_header_and_footer( nwipe_context_t* c, char* page_title )
{
    /**************************************************************************
     * Create header and footer on most recently added page, with the exception
     * of the green tick/red icon which is set from the 'status' section below.
     */

    // Barcode
    snprintf( barcode, sizeof( barcode ), "%s:%s", c->device_model, c->device_serial_no );
    pdf_add_barcode( pdf, NULL, PDF_BARCODE_128A, 100, pica(61), 400, pica(2), barcode, PDF_BLACK );
    
    pdf_set_font( pdf, "Helvetica-Bold" );
    // Drive Model
    snprintf( model_header, sizeof( model_header ), " %s: %s ", "Model", c->device_model );
    pdf_add_text_wrap( pdf, NULL, model_header, 14, 0, pica(59), PDF_BLACK, page_width, PDF_ALIGN_CENTER, &height );
    // Drive Serial No.
    snprintf( serial_header, sizeof( serial_header ), " %s: %s ", "S/N", c->device_serial_no );
    pdf_add_text_wrap( pdf, NULL, serial_header, 14, 0, pica(57.5), PDF_BLACK, page_width, PDF_ALIGN_CENTER, &height );
    
    pdf_set_font( pdf, "Helvetica" );
    pdf_add_text_wrap(
        pdf, NULL, "Drive Data Erasure Report", 18, 0, pica(55), PDF_BLACK, page_width, PDF_ALIGN_CENTER, &height );
    pdf_add_text_wrap(
        pdf, NULL, page_title, 14, 0, pica(53), PDF_BLACK, page_width, PDF_ALIGN_CENTER, &height );
    
    // Logo - Left side
    pdf_add_image_data( pdf, NULL, 45, pica(52), 114, 100, bin2c_wyosupport_jpg, 36710 );
    pdf_add_line( pdf, NULL, 50, pica(51), 550, pica(51), 2, PDF_BLACK );
    
    // Footer
    pdf_add_line( pdf, NULL, 50, pica(4.5), 550, pica(4.5), 2, PDF_BLACK );
    pdf_set_font( pdf, "Helvetica" );
    pdf_add_text_wrap( pdf, NULL, pdf_footer, 8, 0, pica(3), PDF_BLACK, page_width, PDF_ALIGN_CENTER, &height );

    /**********************************************************
     * Display the appropriate status icon, top right on page on
     * most recently added page.
     */
    switch( status_icon )
    {
        case STATUS_ICON_GREEN_TICK:

            /* Display the green tick icon in the header */
            pdf_add_image_data( pdf, NULL, 450, pica(51.5), 100, 100, bin2c_te_jpg, 54896 );
            break;

        case STATUS_ICON_YELLOW_EXCLAMATION:

            /* Display the yellow exclamation icon in the header */
            pdf_add_image_data( pdf, NULL, 450, pica(51.5), 100, 100, bin2c_nwipe_exclamation_jpg, 65791 );
            break;

        case STATUS_ICON_RED_CROSS:

            // Display the red cross in the header
            pdf_add_image_data( pdf, NULL, 450, pica(51.5), 100, 100, bin2c_redcross_jpg, 60331 );
            break;

        default:

            break;
    }
}
