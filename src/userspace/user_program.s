  _start:                           
      # sys_write(1, message, 22)
      mov $1, %rax          # SYS_WRITE                                  
      mov $1, %rdi          # fd = stdout                                
      lea message(%rip), %rsi  # buffer (RIP-relative addressing)        
      mov $22, %rdx         # length                                     
      int $0x80              
                                    
      # sys_exit(0)                                                      
      mov $0, %rax          # SYS_EXIT                                   
      mov $0, %rdi          # exit code = 0                              
      int $0x80                     
                                    
      # Should never reach here     
      jmp .                                                              
                                                                         
  message:                                                               
      .ascii "Hello from USERSPACE!\n" 
