/* Filtering the data received on the UART30*/
/* Strip ANSI escape sequences and shell prompt from a buffer */
static size_t strip_ansi_escapes(const char *src, size_t src_len, char *dst, size_t dst_size)
{
	size_t dst_pos = 0;
	size_t i = 0;

	while (i < src_len && dst_pos < dst_size - 1) {
		/* Check for ESC character (0x1B) */
		if (src[i] == 0x1b) {
			i++;
			if (i < src_len && src[i] == '[') {
				/* CSI sequence: skip until we hit a letter (0x40-0x7E) */
				i++;
				while (i < src_len && (src[i] < 0x40 || src[i] > 0x7E)) {
					i++;
				}
				if (i < src_len) {
					i++; /* Skip the final character */
				}
			}
			/* Other escape types - just skip the ESC */
		} else if (src[i] == '~' && i + 2 < src_len && src[i+1] == '$' && src[i+2] == ' ') {
			/* Skip shell prompt "~$ " */
			i += 3;
		} else if (src[i] == '\r') {
			/* Skip carriage return */
			i++;
		} else {
			dst[dst_pos++] = src[i++];
		}
	}
	dst[dst_pos] = '\0';
	return dst_pos;
}