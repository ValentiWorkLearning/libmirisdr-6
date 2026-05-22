#include "async.h"

static int mirisdr_feed_async(mirisdr_dev_t *p, unsigned char *samples, uint32_t bytes) {
    uint32_t i;

    if (!p) goto failed;
    if (!p->cb) goto failed;

    if (!p->xfer_out_len) {
        p->cb(samples, bytes, p->cb_ctx);
    } else if (p->xfer_out_pos == 0) {
        if (bytes == p->xfer_out_len) {
            p->cb(samples, bytes, p->cb_ctx);
        } else if (bytes < p->xfer_out_len) {
            memcpy(p->xfer_out, samples, bytes);
            p->xfer_out_pos = bytes;
        } else {
            for (i = 0;; i += p->xfer_out_len) {
                if (i + p->xfer_out_len > bytes) {
                    if (bytes > i) {
                        memcpy(p->xfer_out, samples + i, bytes - i);
                        p->xfer_out_pos = bytes - i;
                    }
                    break;
                }
                p->cb(samples + i, p->xfer_out_len, p->cb_ctx);
            }
        }
    } else if (p->xfer_out_pos + bytes == p->xfer_out_len) {
        memcpy(p->xfer_out + p->xfer_out_pos, samples, bytes);
        p->cb(p->xfer_out, p->xfer_out_len, p->cb_ctx);
        p->xfer_out_pos = 0;
    } else if (p->xfer_out_pos + bytes < p->xfer_out_len) {
        memcpy(p->xfer_out + p->xfer_out_pos, samples, bytes);
        p->xfer_out_pos += bytes;
    } else {
        memcpy(p->xfer_out + p->xfer_out_pos, samples, p->xfer_out_len - p->xfer_out_pos);
        p->cb(p->xfer_out, p->xfer_out_len, p->cb_ctx);

        for (i = p->xfer_out_len - p->xfer_out_pos;; i += p->xfer_out_len) {
            if (i + p->xfer_out_len > bytes) {
                if (bytes > i) {
                    memcpy(p->xfer_out, samples + i, bytes - i);
                    p->xfer_out_pos = bytes - i;
                } else {
                    p->xfer_out_pos = 0;
                }
                break;
            }
            p->cb(samples + i, p->xfer_out_len, p->cb_ctx);
        }
    }

    return 0;

failed:
    return -1;
}

static uint8_t *samples_realloc(mirisdr_dev_t *p, int size)
{
    if (!p) return NULL;

    if (p->samples_size < size) {
        uint8_t *new_samples = (uint8_t *)malloc((size_t)size);
        if (!new_samples) {
            return NULL;
        }

        if (p->samples) {
            free(p->samples);
        }

        p->samples = new_samples;
        p->samples_size = size;
    }

    return p->samples;
}

static int mirisdr_submit_and_track(mirisdr_dev_t *p, struct libusb_transfer *xfer)
{
    int r;

    if (!p || !xfer) {
        return LIBUSB_ERROR_INVALID_PARAM;
    }

    r = libusb_submit_transfer(xfer);
    if (r == 0) {
        p->xfers_in_flight++;
    }

    return r;
}

static void mirisdr_mark_callback_done(mirisdr_dev_t *p)
{
    if (!p) return;
    if (p->xfers_in_flight > 0) {
        p->xfers_in_flight--;
    }
}

/* volání pro zasílání dat */
static void LIBUSB_CALL _libusb_callback(struct libusb_transfer *xfer)
{
    size_t i;
    int len, bytes = 0;
    unsigned char *iso_packet_buf = NULL;
    mirisdr_dev_t *p;
    uint8_t *samples;
    int submit_r;

    //printf("Calling libusb callback\r\n");
    if (!xfer) {
        //printf("if (!xfer)\r\n");
        return;
    }

    p = (mirisdr_dev_t *)xfer->user_data;
    if (!p) {
        // printf("if (!p)\r\n");
        return;
    }

    /* This callback corresponds to one previously submitted transfer. */
    mirisdr_mark_callback_done(p);

    samples = p->samples;

    if (p->async_status == MIRISDR_ASYNC_CANCELING ||
        p->async_shutdown_requested) {

        //printf("p->async_status == MIRISDR_ASYNC_CANCELING || p->async_shutdown_requested\r\n");
        return;
    }

    if (xfer->status == LIBUSB_TRANSFER_COMPLETED) {
        switch (xfer->type) {
        case LIBUSB_TRANSFER_TYPE_ISOCHRONOUS:
            switch (p->format) {
            case MIRISDR_FORMAT_252_S16:
                samples = samples_realloc(p, 504 * DEFAULT_ISO_BUFFERS * DEFAULT_ISO_PACKETS * 2);
                if (!samples) goto failed;
                for (i = 0; i < DEFAULT_ISO_PACKETS; i++) {
                    struct libusb_iso_packet_descriptor *packet = &xfer->iso_packet_desc[i];
                    if ((packet->actual_length > 0) &&
                        (iso_packet_buf = libusb_get_iso_packet_buffer_simple(xfer, (unsigned int)i))) {
                        len = mirisdr_samples_convert_252_s16(p, iso_packet_buf, samples + bytes, packet->actual_length);
                        bytes += len;
                    }
                }
                break;

            case MIRISDR_FORMAT_336_S16:
                samples = samples_realloc(p, 672 * DEFAULT_ISO_BUFFERS * DEFAULT_ISO_PACKETS * 2);
                if (!samples) goto failed;
                for (i = 0; i < DEFAULT_ISO_PACKETS; i++) {
                    struct libusb_iso_packet_descriptor *packet = &xfer->iso_packet_desc[i];
                    if ((packet->actual_length > 0) &&
                        (iso_packet_buf = libusb_get_iso_packet_buffer_simple(xfer, (unsigned int)i))) {
                        len = mirisdr_samples_convert_336_s16(p, iso_packet_buf, samples + bytes, packet->actual_length);
                        bytes += len;
                    }
                }
                break;

            case MIRISDR_FORMAT_384_S16:
                samples = samples_realloc(p, 768 * DEFAULT_ISO_BUFFERS * DEFAULT_ISO_PACKETS * 2);
                if (!samples) goto failed;
                for (i = 0; i < DEFAULT_ISO_PACKETS; i++) {
                    struct libusb_iso_packet_descriptor *packet = &xfer->iso_packet_desc[i];
                    if ((packet->actual_length > 0) &&
                        (iso_packet_buf = libusb_get_iso_packet_buffer_simple(xfer, (unsigned int)i))) {
                        len = mirisdr_samples_convert_384_s16(p, iso_packet_buf, samples + bytes, packet->actual_length);
                        bytes += len;
                    }
                }
                break;

            case MIRISDR_FORMAT_504_S16:
                samples = samples_realloc(p, 1008 * DEFAULT_ISO_BUFFERS * DEFAULT_ISO_PACKETS * 2);
                if (!samples) goto failed;
                for (i = 0; i < DEFAULT_ISO_PACKETS; i++) {
                    struct libusb_iso_packet_descriptor *packet = &xfer->iso_packet_desc[i];
                    if ((packet->actual_length > 0) &&
                        (iso_packet_buf = libusb_get_iso_packet_buffer_simple(xfer, (unsigned int)i))) {
                        len = mirisdr_samples_convert_504_s16(p, iso_packet_buf, samples + bytes, packet->actual_length);
                        bytes += len;
                    }
                }
                break;

            case MIRISDR_FORMAT_504_S8:
                samples = samples_realloc(p, 1008 * DEFAULT_ISO_BUFFERS * DEFAULT_ISO_PACKETS);
                if (!samples) goto failed;
                for (i = 0; i < DEFAULT_ISO_PACKETS; i++) {
                    struct libusb_iso_packet_descriptor *packet = &xfer->iso_packet_desc[i];
                    if ((packet->actual_length > 0) &&
                        (iso_packet_buf = libusb_get_iso_packet_buffer_simple(xfer, (unsigned int)i))) {
                        len = mirisdr_samples_convert_504_s8(p, iso_packet_buf, samples + bytes, packet->actual_length);
                        bytes += len;
                    }
                }
                break;

            default:
                goto failed;
            }
            break;

        case LIBUSB_TRANSFER_TYPE_BULK:
            switch (p->format) {
            case MIRISDR_FORMAT_252_S16:
                samples = samples_realloc(p, (DEFAULT_BULK_BUFFER / 1024) * 1008);
                if (!samples) goto failed;
                bytes = mirisdr_samples_convert_252_s16(p, xfer->buffer, samples, xfer->actual_length);
                break;

            case MIRISDR_FORMAT_336_S16:
                samples = samples_realloc(p, (DEFAULT_BULK_BUFFER / 1024) * 1344);
                if (!samples) goto failed;
                bytes = mirisdr_samples_convert_336_s16(p, xfer->buffer, samples, xfer->actual_length);
                break;

            case MIRISDR_FORMAT_384_S16:
                samples = samples_realloc(p, (DEFAULT_BULK_BUFFER / 1024) * 1536);
                if (!samples) goto failed;
                bytes = mirisdr_samples_convert_384_s16(p, xfer->buffer, samples, xfer->actual_length);
                break;

            case MIRISDR_FORMAT_504_S16:
                samples = samples_realloc(p, (DEFAULT_BULK_BUFFER / 1024) * 2016);
                if (!samples) goto failed;
                bytes = mirisdr_samples_convert_504_s16(p, xfer->buffer, samples, xfer->actual_length);
                break;

            case MIRISDR_FORMAT_504_S8:
                samples = samples_realloc(p, (DEFAULT_BULK_BUFFER / 1024) * 1008);
                if (!samples) goto failed;
                bytes = mirisdr_samples_convert_504_s8(p, xfer->buffer, samples, xfer->actual_length);
                break;

            default:
                goto failed;
            }
            break;

        default:
            printf( "not isoc or bulk transfer type on usb device: %u\n", p->index);
            goto failed;
        }

        if (bytes > 0) {
            mirisdr_feed_async(p, samples, (uint32_t)bytes);
        }
        else{
            printf("pupu...There are no bytes...\r\n");
        }

        if (xfer->type == LIBUSB_TRANSFER_TYPE_BULK) {
            if (p->sync_loss_cnt > (int)p->xfer_buf_num) {
                p->sync_loss_cnt = -((int)p->xfer_buf_num) + 1;
                xfer->length = DEFAULT_BULK_BUFFER - 512;
                printf( "libmirisdr: Sync lost. Trying to synchronize.\n\r\n");
            } else {
                xfer->length = DEFAULT_BULK_BUFFER;
            }
        }

        if (p->async_status == MIRISDR_ASYNC_CANCELING ||
            p->async_shutdown_requested) {
            return;
        }

        submit_r = mirisdr_submit_and_track(p, xfer);
        if (submit_r == LIBUSB_ERROR_BUSY) {
            printf( "warning re-submitting URB on device %u: BUSY\n", p->index);
            return;
        }
        if (submit_r == LIBUSB_ERROR_NO_DEVICE) {
            printf( "device disappeared while re-submitting URB on device %u\n", p->index);
            goto failed;
        }
        if (submit_r < 0) {
            printf( "error re-submitting URB on device %u, code %d\n", p->index, submit_r);
            goto failed;
        }

    } else if (xfer->status == LIBUSB_TRANSFER_CANCELLED) {
        return;
    } else {
        printf( "error async transfer status %d on device %u\n", xfer->status, p->index);
        goto failed;
    }

    return;

failed:
    p->async_shutdown_requested = 1;
    mirisdr_cancel_async(p);
    p->async_status = MIRISDR_ASYNC_FAILED;
}

int mirisdr_cancel_async(mirisdr_dev_t *p)
{
    if (!p) goto failed;

    switch (p->async_status) {
    case MIRISDR_ASYNC_INACTIVE:
    case MIRISDR_ASYNC_CANCELING:
        goto canceled;

    case MIRISDR_ASYNC_RUNNING:
    case MIRISDR_ASYNC_PAUSED:
        p->async_shutdown_requested = 1;
        p->async_status = MIRISDR_ASYNC_CANCELING;
        break;

    case MIRISDR_ASYNC_FAILED:
        goto failed;
    }

    return 0;

failed:
    return -1;

canceled:
    return -2;
}

int mirisdr_cancel_async_now(mirisdr_dev_t *p)
{
    struct timeval tv = {0, 20000};

    if (!p) goto failed;

    switch (p->async_status) {
    case MIRISDR_ASYNC_INACTIVE:
        goto done;

    case MIRISDR_ASYNC_CANCELING:
        break;

    case MIRISDR_ASYNC_RUNNING:
    case MIRISDR_ASYNC_PAUSED:
        p->async_shutdown_requested = 1;
        p->async_status = MIRISDR_ASYNC_CANCELING;
        break;

    case MIRISDR_ASYNC_FAILED:
        goto failed;
    }

    while ((p->async_status != MIRISDR_ASYNC_INACTIVE) &&
           (p->async_status != MIRISDR_ASYNC_FAILED)) {
        if (p->ctx) {
            (void)libusb_handle_events_timeout(p->ctx, &tv);
        }
#if defined (_WIN32) && !defined(__MINGW32__)
        Sleep(20);
#else
        usleep(20000);
#endif
    }

done:
    return 0;

failed:
    return -1;
}

static int mirisdr_async_alloc(mirisdr_dev_t *p)
{
    size_t i;

    if (!p) return -1;

    if (!p->xfer) {
        p->xfer = malloc(p->xfer_buf_num * sizeof(*p->xfer));
        if (!p->xfer) return -1;
        memset(p->xfer, 0, p->xfer_buf_num * sizeof(*p->xfer));

        for (i = 0; i < p->xfer_buf_num; i++) {
            switch (p->transfer) {
            case MIRISDR_TRANSFER_BULK:
                p->xfer[i] = libusb_alloc_transfer(0);
                break;
            case MIRISDR_TRANSFER_ISOC:
                p->xfer[i] = libusb_alloc_transfer(DEFAULT_ISO_PACKETS);
                break;
            default:
                return -1;
            }

            if (!p->xfer[i]) {
                return -1;
            }
        }
    }

    if (!p->xfer_buf) {
        p->xfer_buf = malloc(p->xfer_buf_num * sizeof(*p->xfer_buf));
        if (!p->xfer_buf) return -1;
        memset(p->xfer_buf, 0, p->xfer_buf_num * sizeof(*p->xfer_buf));

        for (i = 0; i < p->xfer_buf_num; i++) {
            switch (p->transfer) {
            case MIRISDR_TRANSFER_BULK:
                p->xfer_buf[i] = malloc(DEFAULT_BULK_BUFFER);
                break;
            case MIRISDR_TRANSFER_ISOC:
                p->xfer_buf[i] = malloc(DEFAULT_ISO_BUFFER * DEFAULT_ISO_BUFFERS * DEFAULT_ISO_PACKETS);
                break;
            default:
                return -1;
            }

            if (!p->xfer_buf[i]) {
                return -1;
            }
        }
    }

    if (!p->xfer_out && p->xfer_out_len) {
        p->xfer_out = malloc(p->xfer_out_len * sizeof(*p->xfer_out));
        if (!p->xfer_out) return -1;
    }

    return 0;
}

static int mirisdr_async_free(mirisdr_dev_t *p)
{
    size_t i;

    if (!p) return -1;

    if (p->xfer) {
        for (i = 0; i < p->xfer_buf_num; i++) {
            if (p->xfer[i]) {
                libusb_free_transfer(p->xfer[i]);
                p->xfer[i] = NULL;
            }
        }

        free(p->xfer);
        p->xfer = NULL;
    }

    if (p->xfer_buf) {
        for (i = 0; i < p->xfer_buf_num; i++) {
            if (p->xfer_buf[i]) {
                free(p->xfer_buf[i]);
                p->xfer_buf[i] = NULL;
            }
        }

        free(p->xfer_buf);
        p->xfer_buf = NULL;
    }

    if (p->xfer_out) {
        free(p->xfer_out);
        p->xfer_out = NULL;
    }

    return 0;
}

static void mirisdr_cancel_all_transfers(mirisdr_dev_t *p)
{
    size_t i;

    if (!p || !p->xfer) return;

    for (i = 0; i < p->xfer_buf_num; i++) {
        if (!p->xfer[i]) continue;
        libusb_cancel_transfer(p->xfer[i]);
    }
}

static int mirisdr_wait_all_transfers_done(mirisdr_dev_t *p)
{
    struct timeval tv = {0, 50000};
    int r;

    if (!p) return -1;

    while (p->xfers_in_flight > 0) {
        r = libusb_handle_events_timeout(p->ctx, &tv);
        if (r < 0 && r != LIBUSB_ERROR_INTERRUPTED) {
            printf( "libusb_handle_events returned while draining: %d\n", r);
            return -1;
        }
    }

    return 0;
}

int mirisdr_read_async(mirisdr_dev_t *p, mirisdr_read_async_cb_t cb, void *ctx, uint32_t num, uint32_t len)
{
    size_t i;
    int r;
    struct timeval tv = {1, 0};

    if (!p) goto failed;
    if (!p->dh) goto failed;
    if (p->async_status != MIRISDR_ASYNC_INACTIVE) goto failed;

    p->cb = cb;
    p->cb_ctx = ctx;
    p->xfer_buf_num = (num == 0) ? DEFAULT_BUF_NUMBER : num;
    p->xfer_out_len = (len == 0) ? 0 : len;
    p->xfer_out_pos = 0;
    p->sync_loss_cnt = 0;
    p->xfers_in_flight = 0;
    p->async_shutdown_requested = 0;

    switch (p->transfer) {
    case MIRISDR_TRANSFER_BULK:
        if ((r = libusb_set_interface_alt_setting(p->dh, 0, 3)) < 0) {
            printf( "failed to use alternate setting for Bulk mode on miri usb device %u with code %d\n", p->index, r);
        }
        break;

    case MIRISDR_TRANSFER_ISOC:
        if ((r = libusb_set_interface_alt_setting(p->dh, 0, 1)) < 0) {
            printf( "failed to use alternate setting for Isochronous mode on miri usb device %u with code %d\n", p->index, r);
        }
        break;

    default:
        printf( "unsupported transfer type on miri usb device %u\n", p->index);
        goto failed;
    }

    if (mirisdr_async_alloc(p) < 0) {
        goto failed_free;
    }

    for (i = 0; i < p->xfer_buf_num; i++) {
        switch (p->transfer) {
        case MIRISDR_TRANSFER_BULK:
            libusb_fill_bulk_transfer(p->xfer[i],
                                      p->dh,
                                      0x81,
                                      p->xfer_buf[i],
                                      DEFAULT_BULK_BUFFER,
                                      _libusb_callback,
                                      (void *)p,
                                      DEFAULT_BULK_TIMEOUT);
            break;

        case MIRISDR_TRANSFER_ISOC:
            libusb_fill_iso_transfer(p->xfer[i],
                                     p->dh,
                                     0x81,
                                     p->xfer_buf[i],
                                     DEFAULT_ISO_BUFFER * DEFAULT_ISO_BUFFERS * DEFAULT_ISO_PACKETS,
                                     DEFAULT_ISO_PACKETS,
                                     _libusb_callback,
                                     (void *)p,
                                     DEFAULT_ISO_TIMEOUT);
            libusb_set_iso_packet_lengths(p->xfer[i], DEFAULT_ISO_BUFFER * DEFAULT_ISO_BUFFERS);
            break;

        default:
            printf( "unsupported transfer type\n\r\n");
            goto failed_cancel;
        }

        r = mirisdr_submit_and_track(p, p->xfer[i]);
        if (r < 0) {
            printf( "Failed to submit transfer %lu reason: %d\n", (unsigned long)i, r);
            goto failed_cancel;
        }
    }

    mirisdr_streaming_start(p);
    p->async_status = MIRISDR_ASYNC_RUNNING;

    while (p->async_status != MIRISDR_ASYNC_INACTIVE) {
        r = libusb_handle_events_timeout(p->ctx, &tv);
        if (r < 0) {
            printf( "libusb_handle_events returned: %d\n", r);
            if (r == LIBUSB_ERROR_INTERRUPTED) continue;
            goto failed_cancel;
        }

        if (p->async_status == MIRISDR_ASYNC_CANCELING) {
            mirisdr_cancel_all_transfers(p);

            if (mirisdr_wait_all_transfers_done(p) < 0) {
                goto failed_force_free;
            }

            p->async_status = MIRISDR_ASYNC_INACTIVE;
            break;
        } else if (p->async_status == MIRISDR_ASYNC_FAILED) {
            mirisdr_cancel_all_transfers(p);
            (void)mirisdr_wait_all_transfers_done(p);
            goto failed_force_free;
        }
    }

#if defined (_WIN32) && !defined(__MINGW32__)
    Sleep(20);
#else
    usleep(20000);
#endif
    mirisdr_streaming_stop(p);
    mirisdr_async_free(p);
    return 0;

failed_cancel:
    p->async_shutdown_requested = 1;
    p->async_status = MIRISDR_ASYNC_CANCELING;
    mirisdr_cancel_all_transfers(p);
    (void)mirisdr_wait_all_transfers_done(p);

failed_force_free:
#if defined (_WIN32) && !defined(__MINGW32__)
    Sleep(20);
#else
    usleep(20000);
#endif
    mirisdr_streaming_stop(p);
    mirisdr_async_free(p);

failed_free:
failed:
    p->async_status = MIRISDR_ASYNC_INACTIVE;
    return -1;
}

int mirisdr_start_async(mirisdr_dev_t *p)
{
    size_t i;
    int r;

    if (!p) goto failed;
    if (p->async_status != MIRISDR_ASYNC_PAUSED) goto failed;

    p->xfer_out_pos = 0;
    p->xfers_in_flight = 0;
    p->async_shutdown_requested = 0;

    for (i = 0; i < p->xfer_buf_num; i++) {
        if (!p->xfer[i]) continue;

        r = mirisdr_submit_and_track(p, p->xfer[i]);
        if (r < 0) {
            goto failed;
        }
    }

    if (p->async_status != MIRISDR_ASYNC_PAUSED) goto failed;

    mirisdr_streaming_start(p);
    p->async_status = MIRISDR_ASYNC_RUNNING;
    return 0;

failed:
    return -1;
}

int mirisdr_stop_async(mirisdr_dev_t *p)
{
    if (!p) goto failed;
    if (p->async_status != MIRISDR_ASYNC_RUNNING) goto failed;

    p->async_shutdown_requested = 1;
    p->async_status = MIRISDR_ASYNC_CANCELING;

    mirisdr_cancel_all_transfers(p);

    if (mirisdr_wait_all_transfers_done(p) < 0) {
        goto failed;
    }

#if defined (_WIN32) && !defined(__MINGW32__)
    Sleep(20);
#else
    usleep(20000);
#endif
    mirisdr_streaming_stop(p);

    p->async_status = MIRISDR_ASYNC_PAUSED;
    return 0;

failed:
    return -1;
}