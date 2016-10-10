add_subdirectory(imapx)
add_subdirectory(local)
add_subdirectory(nntp)
add_subdirectory(pop3)
add_subdirectory(smtp)

if(NOT WIN32)
	add_subdirectory(sendmail)
endif(NOT WIN32)