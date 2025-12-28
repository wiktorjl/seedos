# Limine boot protocol markers
# These magic values mark the start and end of the Limine requests section.
# Limine scans the kernel binary for these markers to find protocol requests.

.section .limine_requests_start, "aw", @progbits
.global limine_requests_start_marker
limine_requests_start_marker:
    .quad 0xf6b8f4b39de7d1ae
    .quad 0xfab91a6940fcb9cf
    .quad 0x785c6ed015d3e316
    .quad 0x181e920a7852b9d9

.section .limine_requests_end, "aw", @progbits
.global limine_requests_end_marker
limine_requests_end_marker:
    .quad 0xadc0e0531bb10d03
    .quad 0x9572709f31764c62
