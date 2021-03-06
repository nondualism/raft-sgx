######## Intel(R) SGX SDK Settings ########
ifeq ($(shell getconf LONG_BIT), 32)
	SGX_ARCH := x86
else ifeq ($(findstring -m32, $(CXXFLAGS)), -m32)
	SGX_ARCH := x86
endif

ifeq ($(SGX_ARCH), x86)
	SGX_COMMON_CFLAGS := -m32
	SGX_LIBRARY_PATH := $(SGX_SDK)/lib
	SGX_ENCLAVE_SIGNER := $(SGX_SDK)/bin/x86/sgx_sign
	SGX_EDGER8R := $(SGX_SDK)/bin/x86/sgx_edger8r
else
	SGX_COMMON_CFLAGS := -m64
	SGX_LIBRARY_PATH := $(SGX_SDK)/lib64
	SGX_ENCLAVE_SIGNER := $(SGX_SDK)/bin/x64/sgx_sign
	SGX_EDGER8R := $(SGX_SDK)/bin/x64/sgx_edger8r
endif

ifeq ($(SGX_DEBUG), 1)
ifeq ($(SGX_PRERELEASE), 1)
$(error Cannot set SGX_DEBUG and SGX_PRERELEASE at the same time!!)
endif
endif

ifeq ($(SGX_DEBUG), 1)
        SGX_COMMON_CFLAGS += -O0 -g -DSGX_DEBUG
else
        SGX_COMMON_CFLAGS += -O2
endif

ifeq ($(Debug_off), 1)
	SGX_COMMON_CFLAGS += -DDebug_off
endif
ifeq ($(Warning_off), 1)
	SGX_COMMON_CFLAGS += -DWarning_off
endif
ifeq ($(Error_off), 1)
	SGX_COMMON_CFLAGS += -DError_off
endif

ifneq ($(SGX_MODE), HW)
	Trts_Library_Name := sgx_trts_sim
	Service_Library_Name := sgx_tservice_sim
else
	Trts_Library_Name := sgx_trts
	Service_Library_Name := sgx_tservice
endif

Crypto_Library_Name := sgx_tcrypto
Wolfssl_C_Extra_Flags := -DWOLFSSL_SGX
Wolfssl_C_Extra_Flags += -DWOLFSSL_SHA224
Wolfssl_C_Extra_Flags += -DWOLFSSL_SHA256
Wolfssl_C_Extra_Flags += -DWOLFSSL_SHA384
Wolfssl_C_Extra_Flags += -DWOLFSSL_SHA512
Wolfssl_C_Extra_Flags += -DWOLFSSL_SHA384
Wolfssl_C_Extra_Flags += -DWOLFSSL_SHA3
Wolfssl_C_Extra_Flags += -DWOLFSSL_MD2
Wolfssl_C_Extra_Flags += -DHAVE_CURVE25519
Wolfssl_C_Extra_Flags += -DHAVE_SUPPORTED_CURVES
Wolfssl_C_Extra_Flags += -DUSER_TIME

ifeq ($(SGX_MODE), SIM)
	Wolfssl_C_Extra_Flags += -DSGX_SIM
endif

Wolfssl_Include_Paths := -I$(WOLFSSL_ROOT)/ \
						 -I$(WOLFSSL_ROOT)/wolfcrypt/


Wolfssl_Enclave_C_Files := trusted/Wolfssl_Enclave.c trusted/http_parser.c trusted/raft_node.c trusted/raft_server.c trusted/raft_log.c trusted/raft_server_properties.c
Wolfssl_Enclave_C_Files += trusted/raftee.c
Wolfssl_Enclave_C_Files += trusted/sgx_unsupported.c
Wolfssl_Enclave_C_Files += trusted/https_server.c
Wolfssl_Enclave_C_Files += trusted/tpl/tpl.c
Wolfssl_Enclave_C_Files += trusted/hashmap.c
Wolfssl_Enclave_Include_Paths := -IInclude -Itrusted $(Wolfssl_Include_Paths)\
								   -I$(SGX_SDK)/include -I$(SGX_SDK)/include/tlibc\
								   -I$(SGX_SDK)/include/stlport

Flags_Just_For_C := -Wno-implicit-function-declaration -std=c11
Common_C_Cpp_Flags := $(SGX_COMMON_CFLAGS)  -fvisibility=hidden -fpie $(Wolfssl_Enclave_Include_Paths) -I. -std=c11 -fstack-protector -fno-builtin -fno-builtin-printf -I.
Wolfssl_Enclave_C_Flags := $(Flags_Just_For_C) $(Common_C_Cpp_Flags) $(Wolfssl_C_Extra_Flags)
Wolfssl_Enclave_Cpp_Flags := $(Common_C_Cpp_Flags) $(Wolfssl_C_Extra_Flags)

Wolfssl_Enclave_Link_Flags := $(SGX_COMMON_CFLAGS) -Wl,--no-undefined -nostdlib -nodefaultlibs -nostartfiles -L$(SGX_LIBRARY_PATH) \
	-L$(SGX_WOLFSSL_LIB) -lwolfssl.sgx.static.lib \
	-Wl,--whole-archive -l$(Trts_Library_Name) -Wl,--no-whole-archive \
	-Wl,--start-group -lsgx_tstdc -lsgx_tstdcxx  -lsgx_tkey_exchange -lsgx_tcxx -l$(Crypto_Library_Name) -l$(Service_Library_Name) -Wl,--end-group  \
	-Wl,-Bstatic -Wl,-Bsymbolic -Wl,--no-undefined \
	-Wl,-pie,-eenclave_entry -Wl,--export-dynamic  \
	-Wl,--defsym,__ImageBase=0 \
	-Wl,--version-script=trusted/Wolfssl_Enclave.lds

Wolfssl_Enclave_C_Objects := $(Wolfssl_Enclave_C_Files:.c=.o)

ifeq ($(SGX_MODE), HW)
ifneq ($(SGX_DEBUG), 1)
ifneq ($(SGX_PRERELEASE), 1)
Build_Mode = HW_RELEASE
endif
endif
endif


.PHONY: all run

ifeq ($(Build_Mode), HW_RELEASE)
all: Wolfssl_Enclave.so
	@echo "Build enclave Wolfssl_Enclave.so [$(Build_Mode)|$(SGX_ARCH)] success!"
	@echo
	@echo "*********************************************************************************************************************************************************"
	@echo "PLEASE NOTE: In this mode, please sign the Wolfssl_Enclave.so first using Two Step Sign mechanism before you run the app to launch and access the enclave."
	@echo "*********************************************************************************************************************************************************"
	@echo
else
all: Wolfssl_Enclave.signed.so
endif

run: all
ifneq ($(Build_Mode), HW_RELEASE)
	@$(CURDIR)/app
	@echo "RUN  =>  app [$(SGX_MODE)|$(SGX_ARCH), OK]"
endif

######## Wolfssl_Enclave Objects ########

trusted/Wolfssl_Enclave_t.c: $(SGX_EDGER8R) ./trusted/Wolfssl_Enclave.edl
	@cd ./trusted && $(SGX_EDGER8R) --trusted ../trusted/Wolfssl_Enclave.edl --search-path ../trusted --search-path $(SGX_SDK)/include
	@echo "GEN  =>  $@"

trusted/Wolfssl_Enclave_t.o: ./trusted/Wolfssl_Enclave_t.c
	@$(CC) $(Wolfssl_Enclave_C_Flags) -c $< -o $@
	@echo "CXX   <=  $<"

trusted/%.o: trusted/%.c
	@echo $(CC) $(Wolfssl_Enclave_C_Flags) -c $< -o $@
	@$(CC) $(Wolfssl_Enclave_Cpp_Flags) -c $< -o $@
	@echo "CC  <=  $<"

Wolfssl_Enclave.so: trusted/Wolfssl_Enclave_t.o $(Wolfssl_Enclave_C_Objects)
	@echo "Begin LINK, Flags:"
	@echo $(Wolfssl_Enclave_Link_Flags)@
	@$(CC) $^ -o $@ $(Wolfssl_Enclave_Link_Flags)
	@echo "LINK =>  $@"

Wolfssl_Enclave.signed.so: Wolfssl_Enclave.so
	@$(SGX_ENCLAVE_SIGNER) sign -key trusted/Wolfssl_Enclave_private.pem -enclave Wolfssl_Enclave.so -out $@ -config trusted/Wolfssl_Enclave.config.xml
	@echo "SIGN =>  $@"
clean:
	@rm -f Wolfssl_Enclave.* trusted/Wolfssl_Enclave_t.*  $(Wolfssl_Enclave_C_Objects)
