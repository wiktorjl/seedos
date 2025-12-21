ENTRY(main)

SECTIONS
{
    . = 0xFFFFFFFF80000000;

    .blob : {
        *(*)
    }

    /DISCARD/ : {
    	*(.eh_frame)
    	*(.comment)	
    }
}
