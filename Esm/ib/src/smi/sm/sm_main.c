/* BEGIN_ICS_COPYRIGHT7 ****************************************

Copyright (c) 2015, Intel Corporation

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of Intel Corporation nor the names of its contributors
      may be used to endorse or promote products derived from this software
      without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

** END_ICS_COPYRIGHT7   ****************************************/

/* [ICS VERSION STRING: unknown] */

//===========================================================================//
//									     //
// FILE NAME								     //
//    sm_main.c								     //
//									     //
// DESCRIPTION								     //
//    The main entry point to the OS independent part of SM.		     //
//									     //
// DATA STRUCTURES							     //
//    None								     //
//									     //
// FUNCTIONS								     //
//    sm_main				main entry point		     //
//									     //
// DEPENDENCIES								     //
//    ib_mad.h								     //
//    ib_status.h							     //
//									     //
//									     //
//===========================================================================//

#include "os_g.h"
#include "ib_types.h"
#include "ib_mad.h"
#include "ib_status.h"
#include "cs_g.h"
#include "sm_l.h"
#include "sa_l.h"
#include "sm_dbsync.h"
#include "cs_csm_log.h"
#include "sm_jm.h"
#include "iba/public/imath.h"
#include "if3.h"

#ifndef __VXWORKS__
#include "cs_info_file.h"
#include <oib_utils.h>
#endif

#ifdef CAL_IBACCESS
#include "cal_ibaccess_g.h"
#endif

#include "mal_g.h"

extern int sa_main(void);
extern void sa_main_kill(void);
extern void topology_main_kill(void);
extern void async_main_kill(void);
extern void topology_rcv_kill(void);
extern Status_t pm_main_kill(void);
extern void fe_main_kill(void);

#ifdef __VXWORKS__
#include "icsApi.h"
#include "icsBspUtil.h"
#else
extern int sm_conf_server_init(void);

extern Status_t pm_get_xml_config(void);
extern Status_t pm_initialize_config(void);
extern void pmApplyLogLevelChange(PMXmlConfig_t* new_pm_config);
extern void unified_sm_pm(uint32_t argc, uint8_t ** argv);

extern Status_t fe_initialize_config(FMXmlCompositeConfig_t *xml_config, uint32_t fe_instance);

extern void unified_sm_fe(uint32_t argc, uint8_t ** argv);

#ifdef CAL_IBACCESS
extern void mai_umadt_read_kill(void);
#endif


char hostName[64];
#endif

extern bool_t smCheckServiceId(int vf, uint64_t serviceId, VirtualFabrics_t *VirtualFabrics);

uint32_t    smFabricDiscoveryNeeded=0;
uint64_t	topology_sema_setTime=0;
uint64_t	topology_sema_runTime=0;
uint32_t   	sm_def_mc_group;
extern uint32_t            smDebugPerf;  // control SM performance messages; default is off
extern uint32_t            saDebugPerf;  // control SA performance messages; default is off
extern uint32_t            saDebugRmpp;  // control SA RMPP INFO debug messages; default is off
extern uint32_t            sm_debug;    // SM debug; default is off
extern uint32_t            saRmppCheckSum; // control checksum of SA RMPP responses; default is off;
extern uint8_t             smTerminateAfter; // Used for performance testing.
extern char*               smDumpCounters; // Used for performance testing.
extern uint8_t sa_dynamicPlt[];   // entry zero set to 1 indicates table in use

Pool_t		sm_pool;
Pool_t		sm_xml_pool;

Sema_t		sa_sema;
Lock_t		sa_lock;

LidMap_t	* lidmap = NULL;
cl_qmap_t	* sm_GuidToLidMap;

size_t	g_smPoolSize;

uint8_t		sm_env[32];
uint32_t    sa_max_cntxt;
uint32_t	sm_lid = 0;
uint32_t	sm_state = SM_STATE_NOTACTIVE;
uint32_t	sm_prevState = SM_STATE_NOTACTIVE;
int			sm_saw_another_sm = FALSE;	// did we ever see another SM in fabric
int			sm_hfi_direct_connect = FALSE;
uint64_t	sm_portguid;
uint8_t     sm_default_mkey_protect_level = 1;
uint8_t     sm_mkey_protect_level;
uint16_t    sm_mkey_lease_period;
uint64_t	sm_control_cmd;
uint64_t    sm_masterCheckInterval;
uint32_t    sm_trapThreshold = 0xffffffff;
// The number of traps required to validate a perceived trap rate.
// Or in other words, the sampling  window is sized so as to be large enough to collect
// this many traps before disabling a port.  Larger values increase the accuracy, but also increase
// the delay between a trap surge and a port disable.
uint32_t	sm_trapThreshold_minCount = 0xffffffff;
uint64_t    sm_trapThresholdWindow = 0;
uint32_t	sm_mcDosThreshold;
uint32_t	sm_mcDosAction;
uint64_t	sm_mcDosInterval;
uint32_t	sm_mcast_mlid_table_cap = 0;
uint16_t	sm_masterSmSl = 0;
uint16_t	sm_masterPmSl = 0;
uint8_t		sm_SLtoSC[STL_MAX_SLS];
uint8_t		sm_SCtoSL[STL_MAX_SCS];
bitset_t	sm_linkSLsInuse;
uint8_t		sm_SlBandwidthAllocated[STL_MAX_SLS];
uint8_t     sm_VfBandwidthAllocated[MAX_VFABRICS];

uint32_t	sm_lid_lo = 1;
uint32_t	sm_lid_hi = UNICAST_LID_MAX;

uint32_t	sm_log_level = 1;
uint32_t	sm_log_level_override = 0;
uint32_t	sm_log_masks[VIEO_LAST_MOD_ID+1];
int			sm_log_to_console = 0;
char		sm_config_filename[256];

uint32_t    sm_nodaemon = 1;

uint32_t sm_lmc_0_freeLid_hint = 0;
uint32_t sm_lmc_e0_freeLid_hint = 0;
uint32_t sm_lmc_freeLid_hint = 0;


STL_SM_INFO	sm_smInfo;
uint32_t	sm_masterStartTime;
McGroup_t	*sm_McGroups=0;
uint32_t    sm_numMcGroups = 0;
uint32_t    sm_McGroups_Need_Prog = 0;
Lock_t      sm_McGroups_lock;

uint32_t	sm_useIdealMcSpanningTreeRoot = 1;
uint32_t	sm_mcSpanningTreeRoot_useLeastWorstCaseCost = 0;
uint32_t	sm_mcSpanningTreeRoot_useLeastTotalCost = 1;
/* Minimum cost improvement required to change the mc root switch */
#define		DEFAULT_MCROOT_COST_IMPROVEMENT_PERCENTAGE	50
uint32_t	sm_mcRootCostDeltaThreshold = DEFAULT_MCROOT_COST_IMPROVEMENT_PERCENTAGE;

boolean     sweepsPaused = 0;

SmAdaptiveRouting_t sm_adaptiveRouting;
VfInfo_t vfInfo;

// XML configuration data structure
#ifdef __VXWORKS__
extern FMXmlCompositeConfig_t *xml_config;
static uint32_t    			xml_trace = 0;
extern SMXmlConfig_t 		sm_config;
extern FEXmlConfig_t 		fe_config;
extern PMXmlConfig_t 		pm_config;
extern SMDPLXmlConfig_t 	sm_dpl_config;
extern SMMcastConfig_t 		sm_mc_config;
extern SmMcastMlidShare_t 	sm_mls_config;
extern SMMcastDefGrpCfg_t	sm_mdg_config;
extern VFXmlConfig_t		vf_config;
extern DGXmlConfig_t		dg_config;
extern uint16_t 			numMcGroupClasses;
void sm_cleanGlobals(uint8_t);
#else
FMXmlCompositeConfig_t *xml_config = NULL;
SMXmlConfig_t 				sm_config;
FEXmlConfig_t 				fe_config;
PMXmlConfig_t 				pm_config;
SMDPLXmlConfig_t 			sm_dpl_config;
SMMcastConfig_t 			sm_mc_config;
SmMcastMlidShare_t 			sm_mls_config;
SMMcastDefGrpCfg_t 			sm_mdg_config;
VFXmlConfig_t				vf_config;
DGXmlConfig_t				dg_config;

uint32_t    				xml_trace = 0;

extern uint32_t pm_conf_start;
extern uint32_t bm_conf_start;

static int pm_running = 0;
#ifdef FE_THREAD_SUPPORT_ENABLED
static int fe_running = 0;
#endif
#endif

// Instance of this SM
uint32_t	sm_instance;

// SM Checksums
uint32_t	sm_overall_checksum;
uint32_t	sm_consistency_checksum;

// looptest control
uint32_t sm_looptest_disabled_ar = 0;

#ifdef CONFIG_INCLUDE_DOR
SmDorRouting_t smDorRouting;
#endif

// pointer to Virtual Fabric configuration info
VirtualFabrics_t *initialVfPtr  = NULL;
VirtualFabrics_t *updatedVirtualFabrics = NULL;

IBhandle_t	fd_sa;
IBhandle_t	fd_sa_w;
IBhandle_t	fd_saTrap;
IBhandle_t	fd_async;
IBhandle_t  fd_sminfo;
IBhandle_t	fd_topology;
IBhandle_t	fd_atopology;
IBhandle_t	fd_loopTest;
IBhandle_t	fd_dbsync;

Sema_t		state_sema;
Sema_t		topology_sema;
Sema_t      topology_rcv_sema;          // topology receive thread ready semaphore
Lock_t		old_topology_lock;			// a RW Thread Lock
Lock_t		new_topology_lock;			// a Thread Lock
Lock_t		tid_lock;
Lock_t		handover_sent_lock;


/* flag to indicate if we have triggered a sweep for handover from async thread (sm_fsm.c) */
uint32_t triggered_handover=0;
/* flag to indicate if we have sent a handover handover from the topology thread (sm_topology.c) */
uint32_t handover_sent=0;


SMThread_t	*sm_threads;

static char msgbuf[256]={0};

/*
 * This function sets up a table of values and masks for
 * Multicast group GID's and the maximum number of groups
 * that may exist matching each value/mask pair before
 * the SM starts assigning groups matching the value/mask
 * pair the same multicast lid address.
 *
 * N.B. When we support pkeys, a new key must be added so
 * that we can support associated pkeys.
 */
void sm_init_mcast_mgid_mask_table(void)
{
	IB_GID mask;
	IB_GID value;
	uint16_t maximum;
	Status_t status = 0;
	int i = 0;

	/* Grab MCast Group stuff for each possible entry */
	for (i = 0; i < MAX_SUPPORTED_MCAST_GRP_CLASSES; ++i)
	{
		maximum = 0;

		/* grab the mask */
		if ((status = cs_parse_gid(sm_mls_config.mcastMlid[i].mcastGrpMGidLimitMaskConvert.value, mask.Raw)) != VSTATUS_OK) {
			IB_LOG_ERROR_FMT(__func__, "Bad value for MLIDShare MGIDMask %d: %s", i,
				sm_mls_config.mcastMlid[i].mcastGrpMGidLimitMaskConvert.value);
			continue;
		}

		/* grab the value */
		if ((status = cs_parse_gid(sm_mls_config.mcastMlid[i].mcastGrpMGidLimitValueConvert.value, value.Raw)) != VSTATUS_OK) {
			IB_LOG_ERROR_FMT(__func__, "Bad value for MLIDShare MGIDValue %d: %s", i,
				sm_mls_config.mcastMlid[i].mcastGrpMGidLimitValueConvert.value);
			continue;
		}

		/* grab the limit */
		maximum = 0xFFFF & sm_mls_config.mcastMlid[i].mcastGrpMGidLimitMax;
		if (maximum != 0)
		{
			if ((status = sm_multicast_add_group_class(mask, value, maximum)) != VSTATUS_OK)
				IB_LOG_ERROR_FMT(__func__, "Couldn't add mcast group class # %d to table", i);
		}
	}

	maximum = 0;
	if (sm_mc_config.mcast_mlid_table_cap > MAX_SIZE_MFT)
	{
		IB_LOG_ERROR_FMT(__func__, "Bad value for MLIDTableCap - %d... "
		       "Max allowed is %d, Falling back to default value of %d", sm_mc_config.mcast_mlid_table_cap, MAX_SIZE_MFT, DEFAULT_SW_MLID_TABLE_CAP);
		maximum = DEFAULT_SW_MLID_TABLE_CAP;
	} else
	{
		maximum = (uint16_t)(0xFFFF & sm_mc_config.mcast_mlid_table_cap);
	}
	sm_mcast_mlid_table_cap = maximum;

	IB_LOG_VERBOSE_FMT(__func__,
	       "Calling sm_multicast_set_default_group_class(0, 0, %d)",
	       maximum);

	if ((status = sm_multicast_set_default_group_class(maximum)) != VSTATUS_OK)
	{
		IB_LOG_ERROR_FMT(__func__, "Couldn't add default mcast group class, error = %d", status);
	}
}

/*
 * Read in the dynamic packet lifetime values from the config file
 */
void sm_init_plt_table(void){
	int i, rc=0;
    uint8_t  dplt[DYNAMIC_PACKET_LIFETIME_ARRAY_SIZE];
	char     pltStr[32];

	memset(pltStr,0,sizeof(pltStr));
    sprintf(pltStr,"dynamicPlt");

	if (sm_dpl_config.dp_lifetime[0] > 1) {
		IB_LOG_WARN0("SM: Invalid input specified for DynamicPacketLifetime Enable Must be between 0 or 1. Enabling.");
	}

    dplt[0] = (uint8_t)(sm_dpl_config.dp_lifetime[0]?1:0);
    if (dplt[0]) {
        /* dynamic packet lifetine is on, get the nine values */
        for(i = 1; i < DYNAMIC_PACKET_LIFETIME_ARRAY_SIZE; i++){
            sprintf(pltStr,"dynamicPlt_%.2d",i);
			/* Nota Bene: We don't check for dp_lifetime<DYNAMIC_PLT_MIN 
 			 * because DYNAMIC_PLT_MIN is zero, and dp_lifetime is unsigned. if
 			 * this is changed we will need to check. 
 			 */
            if (sm_dpl_config.dp_lifetime[i] > DYNAMIC_PLT_MAX){
                IB_LOG_WARN_FMT(__func__,
            		"SM: %s %u %s %s %s", "Invalid entry of", (unsigned)sm_dpl_config.dp_lifetime[i], "for", pltStr, "- Must be between 1-31.  Using defaults.");
                rc = 1;
                break;
            } else if (sm_dpl_config.dp_lifetime[i] > 0) {
                dplt[i] = (uint8_t)sm_dpl_config.dp_lifetime[i];
            } else if (i > 1) {
                /* use the previous entry for this index */
                dplt[i] = dplt[i-1];
            } else {
				IB_LOG_WARN0("SM: DynamicPacketLifetime Hops01 unspecified.  Using defaults.");
                /* just break out and use the hard coded defaults */
                rc = 1;
				break;
            }
        }
        if (rc == 0) {
            /* valid set of values entered for dynamic packet lifetime */
            memcpy(sa_dynamicPlt, dplt, sizeof(dplt));
        }
    } else {
        /* turning off dynamic packet lifetime */
        sa_dynamicPlt[0] = 0;
    }
    /* Output what we are using */        
    if (sa_dynamicPlt[0]) {
        sprintf(msgbuf, "SM: Using dynamic packet lifetime values %2d, %2d, %2d, %2d, %2d, %2d, %2d, %2d, %2d",
                sa_dynamicPlt[1], sa_dynamicPlt[2], sa_dynamicPlt[3], sa_dynamicPlt[4], sa_dynamicPlt[5], 
                sa_dynamicPlt[6], sa_dynamicPlt[7], sa_dynamicPlt[8], sa_dynamicPlt[9]);
        vs_log_output_message(msgbuf, FALSE);
    } else {
        sprintf(msgbuf, "SM: Dynamic packet lifetime is OFF, using saPacketLifetime contant %d", (unsigned int)sm_config.sa_packet_lifetime_n2);
        vs_log_output_message(msgbuf, FALSE);
    }
	return;
}

void sm_init_log_setting(void){
#ifndef __VXWORKS__
	vs_log_control(VS_LOG_SETFACILITY, (void *)(unint)getFacility(sm_config.syslog_facility, /* test */ 0), (void *)0, (void *)0);
#endif
	vs_log_control(VS_LOG_SETMASK, sm_log_masks,(void*)(uintn)sm_log_level, (void *)(unint)sm_log_to_console);
#ifndef __VXWORKS__
	if(strlen(sm_config.name) > 0)
		vs_log_control(VS_LOG_SETSYSLOGNAME, (void *)sm_config.name, (void *)0, (void *)0);
	else
		vs_log_control(VS_LOG_SETSYSLOGNAME, (void *)"fm_sm", (void *)0, (void *)0);
	if(strlen(sm_config.log_file) > 0)
	{
		vs_log_control(VS_LOG_SETOUTPUTFILE, (void *)sm_config.log_file, (void *)0, (void *)0);
		if(sm_log_level > 0)
			oib_set_err(vs_log_get_logfile_fd());
		else
			oib_set_err(NULL);

		if(sm_log_level > 2)
			oib_set_dbg(vs_log_get_logfile_fd());
		else
			oib_set_dbg(NULL);
	}
	else
	{
		vs_log_control(VS_LOG_SETOUTPUTFILE, (void *)0, (void *)0, (void *)0);

		if(sm_log_level > 0)
			oib_set_err(OIB_DBG_FILE_SYSLOG);
		else
			oib_set_err(NULL);

		if(sm_log_level > 2)
			oib_set_dbg(OIB_DBG_FILE_SYSLOG);
		else
			oib_set_dbg(NULL);
	}

#endif

	vs_log_set_log_mode(sm_config.syslog_mode);

#ifndef __VXWORKS__
	vs_log_control(VS_LOG_STARTSYSLOG, (void *)0, (void *)0, (void *)0);
#endif
}

void sm_set_log_level(uint32_t log_level)
{
	sm_log_level = log_level;
	sprintf(msgbuf, "Setting SM LogLevel to %u", (unsigned)sm_log_level);
	vs_log_output_message(msgbuf, FALSE);
	cs_log_set_log_masks(sm_log_level, sm_config.syslog_mode, sm_log_masks);
	sm_init_log_setting();
}

uint32_t sm_get_log_level(void)
{
	return sm_log_level;
}

void sm_set_log_mode(uint32_t log_mode)
{
	sm_config.syslog_mode = log_mode;
	sprintf(msgbuf, "Setting SM LogMode to %u", (unsigned)sm_config.syslog_mode);
	vs_log_output_message(msgbuf, FALSE);
	cs_log_set_log_masks(sm_log_level, sm_config.syslog_mode, sm_log_masks);
	sm_init_log_setting();
}

uint32_t sm_get_log_mode(void)
{
	return sm_config.syslog_mode;
}

void sm_set_log_mask(const char* mod, uint32_t mask)
{
	if (! cs_log_get_module_id(mod)) {
		sprintf(msgbuf, "Requested setting SM LogMask for invalid subsystem: %s", mod);
		vs_log_output_message(msgbuf, FALSE);
	} else {
		sprintf(msgbuf, "Setting SM %s_LogMask to 0x%x", mod, (unsigned)mask);
		vs_log_output_message(msgbuf, FALSE);
		cs_log_set_log_mask(mod, mask, sm_log_masks);
		sm_init_log_setting();
	}
}

int sm_valid_module(const char * mod)
{
	return 0 != cs_log_get_module_id(mod);
}

uint32_t sm_get_log_mask(const char* mod)
{
	return cs_log_get_log_mask(mod, sm_log_masks);
}

void sm_set_force_attribute_rewrite(uint32_t force_attr_rewrite){
	sm_config.forceAttributeRewrite = force_attr_rewrite;
	IB_LOG_INFINI_INFO_FMT(__func__, "Setting force attribute rewrite to %u", force_attr_rewrite);
}

void sm_set_skip_attribute_write(char * datap){
    uint32_t tmp;
    memcpy(&tmp, datap, sizeof(tmp));
	sm_config.skipAttributeWrite = tmp;
	IB_LOG_INFINI_INFO_FMT(__func__, "Setting skip attribute write to 0x%x", tmp);
}


#ifdef CONFIG_INCLUDE_DOR
void sm_process_dor_info(SmDorRouting_t *dorCfg) {

	int d,p;
	int warned = 0, toroidal_count = 0;
	smDorRouting = *dorCfg;

	if (smDorRouting.dimensionCount == 0) {
		IB_LOG_ERROR_FMT(__func__,
						 "Routing algorithm configured as dor-updown but no dimensions have been specified in the configuration.");
		IB_FATAL_ERROR("Please specify required dimensions in the configuration. Exiting");
	}

	if (smDorRouting.dimensionCount > SM_DOR_MAX_DIMENSIONS) {
		IB_LOG_ERROR_FMT(__func__, "Number of dimensions configured is %d but dor-updown algorithm supports only %d dimensions.",
						smDorRouting.dimensionCount, SM_DOR_MAX_DIMENSIONS);
		IB_FATAL_ERROR("Please reconfigure with correct number of dimensions. Exiting");
	}

	if (smDorRouting.warn_threshold > SM_DOR_MAX_WARN_THRESHOLD) {
		IB_LOG_WARN_FMT(__func__,
			 "MeshTorusTopology WarnThreshold of %d is higher than max suported %d. Defaulting to %d.",
			 smDorRouting.warn_threshold, smDorRouting.warn_threshold);
		smDorRouting.warn_threshold = SM_DOR_MAX_WARN_THRESHOLD;
	}

	if (smDorRouting.debug)
		IB_LOG_INFINI_INFO_FMT(__func__, "DorTopology %s NumberOfDimensions %d maxQos %d routingSCs %d",
				(smDorRouting.topology == DOR_MESH)?"mesh":((smDorRouting.topology == DOR_TORUS)? "torus":"partial torus"), 
				smDorRouting.dimensionCount, smDorRouting.maxQos, smDorRouting.routingSCs);

	for (d=0; d<smDorRouting.dimensionCount; d++) {
		if (smDorRouting.dimension[d].toroidal)
			toroidal_count++;

		if ((toroidal_count > SM_DOR_MAX_TOROIDAL_DIMENSIONS) && smDorRouting.dimension[d].toroidal) {
			if (!warned) {
				IB_LOG_WARN0("Too many toroidal dimesions found. Only 4 dimesions can be toroidal to ensure cycle free routing.");
				IB_LOG_WARN0("Only the first 4 toroidal dimensions will be routed as toroidal and the rest will be forced to be non-toroidal.");
				warned = 1;
			}
			smDorRouting.dimension[d].toroidal = 0;
		}

		if (!smDorRouting.debug)
			continue;

   	   	IB_LOG_INFINI_INFO_FMT(__func__, "DorDimension %d is %s", 
				d, smDorRouting.dimension[d].toroidal?"toroidal":"not toroidal");

		for (p=0; p<smDorRouting.dimension[d].portCount; p++) {
        	IB_LOG_INFINI_INFO_FMT(__func__, "DorDimension %d PortPair %d,%d", d,
				smDorRouting.dimension[d].portPair[p].port1, smDorRouting.dimension[d].portPair[p].port2);
		}
	}
}
#endif

uint8_t sm_error_check_vfs(VirtualFabrics_t *VirtualFabrics) {

	uint8_t qosError=0;

    if (!VirtualFabrics || (VirtualFabrics->number_of_vfs == 0))  {
        return VSTATUS_BAD;
    }

    // Check for errors - If an error exists, remove All QOS features.

	// MWHEINZ FIXME
	// Should there be a topology specific function for checking vfs errors?

    if (vfInfo.totalSLsNeeded > vfInfo.maxSLs) {
        IB_LOG_ERROR_FMT(__func__, "VFs require too many SLs (%d>%d).  Please reconfigure. Disabling QoS.",vfInfo.totalSLsNeeded, vfInfo.maxSLs);
        qosError = 1;
    }

    if (vfInfo.totalSCsNeeded > (STL_MAX_SCS-1)) {
        IB_LOG_ERROR_FMT(__func__, "VF Routing require too many SCs (%d>%d).  Please reconfigure. Disabling QoS.",vfInfo.totalSCsNeeded, STL_MAX_SCS-1);
        qosError = 1;
    }

    if (vfInfo.activeSLs > vfInfo.totalVFwithConfiguredBW)
        vfInfo.distributedBandwidth = (100 - vfInfo.totalConfiguredBandwidth) / (vfInfo.activeSLs - vfInfo.totalVFwithConfiguredBW);

	return qosError;
}

void sm_printf_vf_debug(VirtualFabrics_t *VirtualFabrics) {

    int vf;
    for (vf=0; vf<VirtualFabrics->number_of_vfs; vf++) {
        IB_LOG_INFINI_INFO_FMT_VF(VirtualFabrics->v_fabric[vf].name, "",
            "Base SL:%d Base SC:%d NumScs:%d QOS:%d HP:%d",
            VirtualFabrics->v_fabric[vf].base_sl, VirtualFabrics->v_fabric[vf].base_sc,
            VirtualFabrics->v_fabric[vf].routing_scs,
            VirtualFabrics->v_fabric[vf].qos_enable, VirtualFabrics->v_fabric[vf].priority);
    }

    if (sm_config.sm_debug_vf)
    {
        // Dump SC to SL
        IB_LOG_INFINI_INFO_FMT(__func__, "SCSL = 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x",
                               sm_SCtoSL[0], sm_SCtoSL[1], sm_SCtoSL[2], sm_SCtoSL[3],
                               sm_SCtoSL[4], sm_SCtoSL[5], sm_SCtoSL[6], sm_SCtoSL[7]);

        IB_LOG_INFINI_INFO_FMT(__func__, "       0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x",
                               sm_SCtoSL[8], sm_SCtoSL[9], sm_SCtoSL[10], sm_SCtoSL[11],
                               sm_SCtoSL[12], sm_SCtoSL[13], sm_SCtoSL[14], sm_SCtoSL[15]);

        IB_LOG_INFINI_INFO_FMT(__func__, "       0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x",
                               sm_SCtoSL[16], sm_SCtoSL[17], sm_SCtoSL[18], sm_SCtoSL[19],
                               sm_SCtoSL[20], sm_SCtoSL[21], sm_SCtoSL[22], sm_SCtoSL[23]);

        IB_LOG_INFINI_INFO_FMT(__func__, "       0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x",
                               sm_SCtoSL[24], sm_SCtoSL[25], sm_SCtoSL[26], sm_SCtoSL[27],
                               sm_SCtoSL[28], sm_SCtoSL[29], sm_SCtoSL[30], sm_SCtoSL[31]);

        // Dump SL to SC
        IB_LOG_INFINI_INFO_FMT(__func__, "SLSC = 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x",
                               sm_SLtoSC[0], sm_SLtoSC[1], sm_SLtoSC[2], sm_SLtoSC[3],
                               sm_SLtoSC[4], sm_SLtoSC[5], sm_SLtoSC[6], sm_SLtoSC[7]);

        IB_LOG_INFINI_INFO_FMT(__func__, "       0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x",
                               sm_SLtoSC[8], sm_SLtoSC[9], sm_SLtoSC[10], sm_SLtoSC[11],
                               sm_SLtoSC[12], sm_SLtoSC[13], sm_SLtoSC[14], sm_SLtoSC[15]);

        IB_LOG_INFINI_INFO_FMT(__func__, "       0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x",
                               sm_SLtoSC[16], sm_SLtoSC[17], sm_SLtoSC[18], sm_SLtoSC[19],
                               sm_SLtoSC[20], sm_SLtoSC[21], sm_SLtoSC[22], sm_SLtoSC[23]);

        IB_LOG_INFINI_INFO_FMT(__func__, "       0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x",
                               sm_SLtoSC[24], sm_SLtoSC[25], sm_SLtoSC[26], sm_SLtoSC[27],
                               sm_SLtoSC[28], sm_SLtoSC[29], sm_SLtoSC[30], sm_SLtoSC[31]);

    }
}

void sm_resolve_pkeys_for_vfs(VirtualFabrics_t *VirtualFabrics) {

	int           vf, vf2;
    int           defaultPkey=0;
    VFDg_t*       mcastGrpp;
    VFAppMgid_t*  mgidp;
    uint8_t       logMcRateChange, logMcMtuChange;

    if (!VirtualFabrics || (VirtualFabrics->number_of_vfs == 0))  {
        return;
    }

    // Prior code dealing with PKEYs
	for (vf=0; vf<VirtualFabrics->number_of_vfs; vf++) {

		// Check if pkey is defined.
		if ((VirtualFabrics->v_fabric[vf].pkey == UNDEFINED_PKEY) ||
			(PKEY_VALUE(VirtualFabrics->v_fabric[vf].pkey) == INVALID_PKEY)) {

			for (mcastGrpp = VirtualFabrics->v_fabric[vf].default_group; mcastGrpp;
			 	 mcastGrpp = mcastGrpp->next_default_group) {

				if (mcastGrpp->def_mc_create &&
					(mcastGrpp->def_mc_pkey != UNDEFINED_PKEY)) {
					VirtualFabrics->v_fabric[vf].pkey = PKEY_VALUE(mcastGrpp->def_mc_pkey);
					break;
				}
			}

			if ((VirtualFabrics->v_fabric[vf].pkey == UNDEFINED_PKEY) ||
				(PKEY_VALUE(VirtualFabrics->v_fabric[vf].pkey) == INVALID_PKEY)) {

				if (!VirtualFabrics->securityEnabled) {
					VirtualFabrics->v_fabric[vf].pkey = DEFAULT_PKEY;

				} else if (VirtualFabrics->v_fabric[vf].security) {
					// Assign unique pkey.
					VirtualFabrics->v_fabric[vf].pkey = bitset_find_first_one(&vfInfo.freePkeys);
					bitset_clear(&vfInfo.freePkeys, VirtualFabrics->v_fabric[vf].pkey);
	
				} else {
					if (defaultPkey == 0) {
						defaultPkey = bitset_find_first_one(&vfInfo.freePkeys);
						bitset_clear(&vfInfo.freePkeys, defaultPkey);
					}
					VirtualFabrics->v_fabric[vf].pkey = defaultPkey;
				}
			}

        	IB_LOG_INFINI_INFO_FMT_VF( VirtualFabrics->v_fabric[vf].name, "sm_resolve_pkeys_for_vfs",
					"VFabric has undefined pkey. Assigning pkey 0x%x.", VirtualFabrics->v_fabric[vf].pkey);

		} else if (!VirtualFabrics->v_fabric[vf].security) {
			for (vf2=0; vf2<VirtualFabrics->number_of_vfs; vf2++) {
				if (vf==vf2) continue;
				if ((VirtualFabrics->v_fabric[vf].pkey == VirtualFabrics->v_fabric[vf2].pkey) && VirtualFabrics->v_fabric[vf2].security) {
        			IB_LOG_INFINI_INFO_FMT_VF( VirtualFabrics->v_fabric[vf].name, "sm_resolve_pkeys_for_vfs", "VFabric has security disabled. VFabric %s has same PKey with security enabled. Enabling security.", 
 						VirtualFabrics->v_fabric[vf2].name);
					VirtualFabrics->v_fabric[vf].security = 1;
					break;
				}
			}
		}

		if (VirtualFabrics->v_fabric[vf].security) {
			VirtualFabrics->v_fabric[vf].pkey = PKEY_VALUE(VirtualFabrics->v_fabric[vf].pkey);
		} else {
			VirtualFabrics->v_fabric[vf].pkey = VirtualFabrics->v_fabric[vf].pkey | FULL_MEMBER;
		}
	
		if (VirtualFabrics->v_fabric[vf].apps.select_sa) {
			if (PKEY_VALUE(VirtualFabrics->v_fabric[vf].pkey) != DEFAULT_PKEY) {
				IB_LOG_ERROR_FMT_VF( VirtualFabrics->v_fabric[vf].name, "sm_resolve_pkeys_for_vfs",
					"VFabric has application SA selected, bad PKey configured 0x%x, must use Default PKey.", VirtualFabrics->v_fabric[vf].pkey);
			} else {
				sm_masterSmSl = VirtualFabrics->v_fabric[vf].base_sl;
			}
		}
		if (smCheckServiceId(vf, PM_SERVICE_ID, VirtualFabrics)) {
			if (PKEY_VALUE(VirtualFabrics->v_fabric[vf].pkey) != DEFAULT_PKEY) {
				IB_LOG_ERROR_FMT_VF( VirtualFabrics->v_fabric[vf].name, "sm_resolve_pkeys_for_vfs",
					"VFabric has application PA selected, bad PKey configured 0x%x, must use Default PKey.", VirtualFabrics->v_fabric[vf].pkey);
			}
		}
		if (VirtualFabrics->v_fabric[vf].apps.select_pm) {
			if (PKEY_VALUE(VirtualFabrics->v_fabric[vf].pkey) != DEFAULT_PKEY) {
				IB_LOG_ERROR_FMT_VF( VirtualFabrics->v_fabric[vf].name, "sm_resolve_pkeys_for_vfs",
					"VFabric has application PM selected, bad PKey configured 0x%x, must use Default PKey.", VirtualFabrics->v_fabric[vf].pkey);
			} else sm_masterPmSl = VirtualFabrics->v_fabric[vf].base_sl;
		}

		logMcRateChange = 1;
		logMcMtuChange = 1;
		for (mcastGrpp = VirtualFabrics->v_fabric[vf].default_group; mcastGrpp;
			 mcastGrpp = mcastGrpp->next_default_group) {

			if (mcastGrpp->def_mc_create) {
				if (mcastGrpp->def_mc_pkey == UNDEFINED_PKEY) {
					mcastGrpp->def_mc_pkey = VirtualFabrics->v_fabric[vf].pkey;
				}

				if (PKEY_VALUE(mcastGrpp->def_mc_pkey) != PKEY_VALUE(VirtualFabrics->v_fabric[vf].pkey)) {
					IB_LOG_ERROR_FMT_VF( VirtualFabrics->v_fabric[vf].name, "sm_resolve_pkeys_for_vfs",
							"MulticastGroup configuration error, mismatch on pkey. Disabling Default Group", 0);
					mcastGrpp->def_mc_create = 0;
				}

				if (mcastGrpp->def_mc_rate_int >= UNDEFINED_XML8) {
					mcastGrpp->def_mc_rate_int = linkrate_gt(IB_STATIC_RATE_100G, VirtualFabrics->v_fabric[vf].max_rate_int) ?
														VirtualFabrics->v_fabric[vf].max_rate_int : IB_STATIC_RATE_100G;
				} else if (linkrate_gt(mcastGrpp->def_mc_rate_int, VirtualFabrics->v_fabric[vf].max_rate_int)) {
					mcastGrpp->def_mc_rate_int = linkrate_gt(IB_STATIC_RATE_100G, VirtualFabrics->v_fabric[vf].max_rate_int) ?
														VirtualFabrics->v_fabric[vf].max_rate_int : IB_STATIC_RATE_100G;
					if (logMcRateChange) {
						logMcRateChange = 0;
						IB_LOG_WARN_FMT_VF( VirtualFabrics->v_fabric[vf].name, "sm_resolve_pkeys_for_vfs",
							"MulticastGroup configuration error, dropping rate to value consistent with vFabric (%dg).",
							linkrate_to_ordinal(mcastGrpp->def_mc_rate_int)/10);
					}
				}

				if (mcastGrpp->def_mc_mtu_int >= UNDEFINED_XML8) {
					mcastGrpp->def_mc_mtu_int = MIN(IB_MTU_2048, VirtualFabrics->v_fabric[vf].max_mtu_int);

				} else if (mcastGrpp->def_mc_mtu_int > VirtualFabrics->v_fabric[vf].max_mtu_int) {
					mcastGrpp->def_mc_mtu_int = MIN(IB_MTU_2048, VirtualFabrics->v_fabric[vf].max_mtu_int);
					if (logMcMtuChange) {
						logMcMtuChange = 0;
						IB_LOG_WARN_FMT_VF( VirtualFabrics->v_fabric[vf].name, "sm_resolve_pkeys_for_vfs",
							"MulticastGroup configuration error, dropping MTU to value consistent with vFabric (%d).",
						Decode_MTU_To_Int(mcastGrpp->def_mc_mtu_int));
					}
				}

				if (mcastGrpp->def_mc_sl == UNDEFINED_XML8) {
					mcastGrpp->def_mc_sl = VirtualFabrics->v_fabric[vf].base_sl;

				} else if (mcastGrpp->def_mc_sl != VirtualFabrics->v_fabric[vf].base_sl) {
					IB_LOG_ERROR_FMT_VF(VirtualFabrics->v_fabric[vf].name,
							"sm_resolve_pkeys_for_vfs", "MulticastGroup configuration error, SL must match BaseSL %d (configured SL %d), disabling Default Group",
							VirtualFabrics->v_fabric[vf].base_sl, mcastGrpp->def_mc_sl);
					mcastGrpp->def_mc_create = 0;
				}

				cl_map_item_t* cl_map_item;
				for_all_qmap_ptr(&mcastGrpp->mgidMap, cl_map_item, mgidp) {
					// Verify mgid has pkey inserted.
					smVerifyMcastPkey(mgidp->mgid, mcastGrpp->def_mc_pkey);
					if (smVFValidateMcDefaultGroup(vf, mgidp->mgid) != VSTATUS_OK) {
						IB_LOG_ERROR_FMT_VF( VirtualFabrics->v_fabric[vf].name,
								"sm_resolve_pkeys_for_vfs", "MulticastGroup configuration error, MGID "FMT_GID" does not match app, disabling Default Group",
								mgidp->mgid[0], mgidp->mgid[1]);
						mcastGrpp->def_mc_create = 0;
					}
				}
			}
		}
	}
}

void sm_allocate_mem_vfs(void) {
	vfInfo.maxSLs = MAX_SLS; // Limited to IB SLs, later increase to STL SLs

    bitset_init(&sm_pool, &sm_linkSLsInuse, STL_MAX_SLS);
    bitset_init(&sm_pool, &vfInfo.freePkeys, SM_PKEYS+1);
}

void sm_init_vf_info(void) {
	vfInfo.totalVFsNoQos = 0;
	vfInfo.activeVFsNoQos = 0;
	vfInfo.totalVFsQos = 0;
	vfInfo.activeVFsQos = 0;
	vfInfo.totalSCsNeeded = 0;
	vfInfo.numHighPriority = 0;
	vfInfo.totalConfiguredBandwidth = 0;
	vfInfo.totalVFwithConfiguredBW = 0;
	vfInfo.defaultRoutingSCs = 1;
	vfInfo.totalSLsNeeded = 0;
	vfInfo.distributedBandwidth = 0;
	vfInfo.numHighPriority = 0;
	vfInfo.maxSLs = MAX_SLS; // Limited to IB SLs, later increase to STL SLs

    memset(sm_SlBandwidthAllocated, 0, sizeof(sm_SlBandwidthAllocated));
    memset(sm_VfBandwidthAllocated, 0, sizeof(sm_VfBandwidthAllocated));

    // FIXME: Setting this (above) to 0, or otherwise changing it while running 
    // affects bufercontrol and SLL. (also affects DOR)
    // I think there are multiple ways of addressing this, but the easiest, and using the layered approach is
    // to stuff these in the VFInfo, let the VF setup routines do what they do, and then
    // copy the results back. 
    // Then we could really see what is what...
    // Maybe do that with all these globals:
    // sm_SlBandwidthAllocated - isolated to VF init, and DOR
    // sm_VfBandwidthAllocated - VF Init, Bfrctrl, SLL
    // sm_linkSLsInuse - VF Init, and used as a temporary variable in sm_shortestpath (S/B fixed!)
    // sm_SLtoSC - VF Init, slsc maps [s/b copied to qos maps, then this would be VF Only]
    // sm_SCtoSL - VF Init, slsc maps [s/b copied to qos maps, then this would be VF Only]
    // sm_SCsInUse - VF Init only
    // sm_HighPrioritySCs - VF Init only.
    // sm_LowPrioritySCs - VF Init only.

    memset(sm_SLtoSC, 15, sizeof(sm_SLtoSC));
    memset(sm_SCtoSL, 0xff, sizeof(sm_SCtoSL));

    bitset_set_all(&vfInfo.freePkeys);
    bitset_clear(&vfInfo.freePkeys, 0);

	//clear SLs in use
    bitset_clear_all(&sm_linkSLsInuse);
}

void sm_free_vf_mem(void) {
	bitset_free(&sm_linkSLsInuse);
	bitset_free(&vfInfo.freePkeys);
}

void sm_update_bw(VirtualFabrics_t *VirtualFabrics) {

    int vf;

    if (!VirtualFabrics || (VirtualFabrics->number_of_vfs_all == 0))  {
        return;
    }

	IB_LOG_INFINI_INFO_FMT("", "VF Bandwidth Allocations :");

    // Assign / Update BW
    for (vf=0; vf < VirtualFabrics->number_of_vfs_all; vf++) {

		if (!VirtualFabrics->v_fabric_all[vf].standby) {

			if (VirtualFabrics->v_fabric_all[vf].qos_enable) {
				if (VirtualFabrics->v_fabric_all[vf].priority) {
					IB_LOG_INFINI_INFO_FMT_VF(VirtualFabrics->v_fabric_all[vf].name, "",
											  "High Priority VF; HP VFs don't participate in BW allocations");
				}
				else {
					// Assign unconfigured BW for QOS fabrics (each VF gets a distribution)
					if (VirtualFabrics->v_fabric_all[vf].percent_bandwidth == UNDEFINED_XML8) {
						VirtualFabrics->v_fabric_all[vf].percent_bandwidth = vfInfo.distributedBandwidth;
					}
					IB_LOG_INFINI_INFO_FMT_VF(VirtualFabrics->v_fabric_all[vf].name, "",
											  "BW:%d%%", VirtualFabrics->v_fabric_all[vf].percent_bandwidth);
				}

			} else {
				// Assign unconfigured BW for non-QOS fabrics (All non QOS share a distribution)
				VirtualFabrics->v_fabric_all[vf].percent_bandwidth = vfInfo.distributedBandwidth;

				IB_LOG_INFINI_INFO_FMT_VF(VirtualFabrics->v_fabric_all[vf].name, "",
										  "Sharing %d%% BW among remaining VFs", VirtualFabrics->v_fabric_all[vf].percent_bandwidth);
			}
		}
		else {
			VirtualFabrics->v_fabric_all[vf].percent_bandwidth = 0;
		}

		// If none of the cases above, means that percent bandwidth was user defined for the VF (QOS)
		//  OR the VF was High Priority (also QOS)

		sm_SlBandwidthAllocated[VirtualFabrics->v_fabric_all[vf].base_sl] = VirtualFabrics->v_fabric_all[vf].percent_bandwidth;
		sm_VfBandwidthAllocated[vf] = VirtualFabrics->v_fabric_all[vf].percent_bandwidth;

		// Override for high priority to have 100% for VF/Bfr Alloc considerations
		if (VirtualFabrics->v_fabric_all[vf].priority==1) sm_VfBandwidthAllocated[vf] = 100;

		//update active list
		int activeVfIdx;
		if (!VirtualFabrics->v_fabric_all[vf].standby) {
			activeVfIdx = findVfIdxInActiveList(&VirtualFabrics->v_fabric_all[vf], VirtualFabrics, TRUE);

			if (activeVfIdx != -1) 
				VirtualFabrics->v_fabric[activeVfIdx].percent_bandwidth = VirtualFabrics->v_fabric_all[vf].percent_bandwidth;
		}


		//Update BWs in vf config
		int vfIdx=0;
		int vfToProcess = -1;
		VFConfig_t* vfp;
		//find the VF name in the global vf_config
		for (vfIdx=0; vfIdx<vf_config.number_of_vfs; ++vfIdx) {
			vfp = vf_config.vf[vfIdx];
			if (strncmp(&vfp->name[0], &VirtualFabrics->v_fabric_all[vf].name[0], MAX_VFABRIC_NAME+1) == 0) {
				vfToProcess = vfIdx;
				break;
			}
		}

		if (vfToProcess != -1) {
			vf_config.vf[vfToProcess]->percent_bandwidth = VirtualFabrics->v_fabric_all[vf].percent_bandwidth;
		}

    } //end loop on all VFs

}

void sm_assign_scs_to_sls(VirtualFabrics_t *VirtualFabrics) {

    // Assign SCs to SLs
    int sc, i, vf;
    bitset_t usedSLs;

    if (!bitset_init(&sm_pool, &usedSLs, STL_MAX_SLS)) {
        IB_FATAL_ERROR("sm_assign_scs_to_sls_normal: No memory for SLs setup, exiting.");
    }

    for (sc=0, vf=0; vf < VirtualFabrics->number_of_vfs; vf++) {

        // Assign the SC when a new SL is encountered.
        if (!bitset_test(&usedSLs, VirtualFabrics->v_fabric[vf].base_sl) ) {
            bitset_set(&usedSLs, VirtualFabrics->v_fabric[vf].base_sl);

            for (i=0; i < VirtualFabrics->v_fabric[vf].routing_scs; i++, sc++) {
                if (sc == 15) sc++; // Skip invalid SC
                if (i==0) {
                    VirtualFabrics->v_fabric[vf].base_sc = sc;
                    sm_SLtoSC[VirtualFabrics->v_fabric[vf].base_sl] = sc;
                }
                if (sc>=STL_MAX_SCS) {
                    IB_LOG_ERROR_FMT(__func__, "%s - Base SL:%d, Base SC:%d, Route Scs: %d. Exceeding number of SCs (%d)",
                                      VirtualFabrics->v_fabric[vf].name, VirtualFabrics->v_fabric[vf].base_sl,
                                      VirtualFabrics->v_fabric[vf].base_sc, VirtualFabrics->v_fabric[vf].routing_scs, sc);
                    continue;
                }

                sm_SCtoSL[sc] = VirtualFabrics->v_fabric[vf].base_sl;
            }
        } else {
            // This SL has already been assigned an SC; copy it to this VF
            VirtualFabrics->v_fabric[vf].base_sc = sm_SLtoSC[VirtualFabrics->v_fabric[vf].base_sl];
        }
    }

    bitset_free(&usedSLs);

    sm_setup_SC2VL(VirtualFabrics);

    sm_printf_vf_debug(VirtualFabrics);
}


void sm_assign_scs_to_sls_FixedMap(VirtualFabrics_t *VirtualFabrics) {

    // Assign SCs to SLs
    sm_setup_SC2VLFixedMap(sm_config.min_supported_vls, VirtualFabrics);

    sm_printf_vf_debug(VirtualFabrics);

}

void sm_assign_base_sls(VirtualFabrics_t *VirtualFabrics) {

    int base_sl;
    int qosSL = -1;
	int vf;
    bitset_t usedSLs;

    if (!VirtualFabrics || (VirtualFabrics->number_of_vfs == 0) || (VirtualFabrics->number_of_vfs_all == 0) )  {
        return;
    }

    bitset_init(&sm_pool, &usedSLs, vfInfo.maxSLs);

    // Assign user specified SLs first.
    for (vf=0; vf < VirtualFabrics->number_of_vfs_all; vf++) {
        if (!VirtualFabrics->v_fabric_all[vf].qos_enable) continue;
        if (VirtualFabrics->v_fabric_all[vf].base_sl != UNDEFINED_XML8) {
            if (bitset_test(&usedSLs, (unsigned)VirtualFabrics->v_fabric_all[vf].base_sl) ) {
                IB_LOG_WARN_FMT(__func__,
                    "VFabric %s (QOS) Ignoring requested base SL %d, as that sl is already assigned to a different vFabric.",
                    VirtualFabrics->v_fabric_all[vf].name, (unsigned)VirtualFabrics->v_fabric_all[vf].base_sl);
                VirtualFabrics->v_fabric_all[vf].base_sl = UNDEFINED_XML8;
            } else {
                bitset_set(&usedSLs, VirtualFabrics->v_fabric_all[vf].base_sl);
            }
        }
    }

    // Now assign the unspecified base SLs. (Which includes the conflict resolution from above)
    // Assign SLs in the order they appear in the config file.
    // [This provides a predicable output for users.]
    for (vf=0; vf < VirtualFabrics->number_of_vfs_all; vf++){
        if (VirtualFabrics->v_fabric_all[vf].base_sl == UNDEFINED_XML8) {
            if (VirtualFabrics->v_fabric_all[vf].qos_enable){
                base_sl = bitset_find_first_zero(&usedSLs);
                VirtualFabrics->v_fabric_all[vf].base_sl = base_sl;
                bitset_set(&usedSLs, base_sl);
            } else {
                // Assign the non-qos SL if not assigned yet.
                if (qosSL == -1) {
                    qosSL = bitset_find_first_zero(&usedSLs);
                    bitset_set(&usedSLs, qosSL);
                }
                VirtualFabrics->v_fabric_all[vf].base_sl = qosSL;
            }
        }
    }

	// Assign SLs to the active VF list
	uint32_t activeVfIdx;

	for (vf=0; vf < VirtualFabrics->number_of_vfs_all; vf++){

		if (!VirtualFabrics->v_fabric_all[vf].standby) {
			activeVfIdx = findVfIdxInActiveList(&VirtualFabrics->v_fabric_all[vf], VirtualFabrics, TRUE);

			if (activeVfIdx != -1) 
				VirtualFabrics->v_fabric[activeVfIdx].base_sl = VirtualFabrics->v_fabric_all[vf].base_sl;
		}
	}

	bitset_free(&usedSLs);

    IB_LOG_INFINI_INFO_FMT(__func__, "%d active VF(s), %d standby VF(s) requires %d SLs and %d SCs for operation",
						   VirtualFabrics->number_of_vfs, VirtualFabrics->number_of_vfs_all-VirtualFabrics->number_of_vfs, 
						   vfInfo.totalSLsNeeded, vfInfo.totalSCsNeeded);

#ifdef CONFIG_INCLUDE_DOR
    smDorRouting.scsNeeded = vfInfo.totalSCsNeeded;
#endif
}

void sm_process_vf_qos_params(VirtualFabrics_t *VirtualFabrics) {

	int vf;
	VF_t* vfp;

    if (!VirtualFabrics || (VirtualFabrics->number_of_vfs_all == 0))  {
        return;
    }

    VirtualFabrics->securityEnabled = 0;
    VirtualFabrics->qosEnabled = 0;

    for (vf=0; vf < VirtualFabrics->number_of_vfs_all; vf++) {

		vfp = &VirtualFabrics->v_fabric_all[vf];

		VFDg_t*         mcastGrpp;

		vfInfo.defaultRoutingSCs = MAX(vfInfo.defaultRoutingSCs, vfp->routing_scs);

		if (PKEY_VALUE(vfp->pkey) < SM_PKEYS) {
			bitset_clear(&vfInfo.freePkeys, PKEY_VALUE(vfp->pkey));
		}

		for (mcastGrpp = vfp->default_group; mcastGrpp;
			 mcastGrpp = mcastGrpp->next_default_group) {
			if (mcastGrpp->def_mc_create &&
				(PKEY_VALUE(mcastGrpp->def_mc_pkey) < SM_PKEYS)) {
				bitset_clear(&vfInfo.freePkeys, PKEY_VALUE(vfp->pkey));
			}
		}

		if (vfp->security) {
			VirtualFabrics->securityEnabled = 1;
		}

        if (!VirtualFabrics->v_fabric_all[vf].qos_enable) {

            vfInfo.totalVFsNoQos++;

			if (!VirtualFabrics->v_fabric_all[vf].standby)
				vfInfo.activeVFsNoQos++;

			// Check for invalid configurations when QOS is disabled.

			// By design, we are requiring QOS to be enabled for preemption
			if (vfp->preempt_rank != 0) {
				IB_LOG_WARN_FMT(__func__,
								"vFabric %s Ignoring requested preempt rank, as QoS not enabled for this vFabric.",
								vfp->name);
				vfp->preempt_rank = 0;
			}

			// By design, we are requiring QoS to be enabled before user is allowed to disable the reliable link level flow control
			if (vfp->flowControlDisable) {
				IB_LOG_WARN_FMT(__func__,
								"vFabric %s Ignoring requested flowControlDisable, as QoS not enabled for this vFabric.",
								vfp->name);
				vfp->flowControlDisable = 0;
			}

			// By design, we require QOS to be enabled to specifiy a specific SL
			if (vfp->base_sl != UNDEFINED_XML8) {
				IB_LOG_WARN_FMT(__func__,
								"vFabric %s Ignoring requested SL, as QoS not enabled for this vFabric.",
								vfp->name);
				vfp->base_sl = UNDEFINED_XML8;
			}

			// By design, QOS must be enabled for high priority to be effective.
			if (vfp->priority == 1) {
				IB_LOG_WARN_FMT(__func__,
								"vFabric %s Ignoring requested Priority setting, as QoS not enabled for this vFabric.",
								vfp->name);
				vfp->priority = 0;
			}

			// Alert the user that user specified BW is ignored if no QOS
			if (vfp->percent_bandwidth != UNDEFINED_XML8) {
				IB_LOG_WARN_FMT(__func__,
								"vFabric %s Ignoring requested BW setting, as QOS not enabled for this vFabric.",
								vfp->name);
			}

			//set percent_bandwidth to 0 for non QoS VF
            VirtualFabrics->v_fabric_all[vf].percent_bandwidth = 0;

        } //end if !vfp->qos_enabled
		else {

            VirtualFabrics->qosEnabled = 1;

            vfInfo.totalVFsQos++;

			if (!VirtualFabrics->v_fabric_all[vf].standby)
				vfInfo.activeVFsQos++;	

            vfInfo.totalSCsNeeded += vfp->routing_scs;

            // Check for invalid SL request.
            if ((vfp->base_sl != UNDEFINED_XML8) &&
                (vfp->base_sl > (vfInfo.maxSLs-1))) {
                IB_LOG_WARN_FMT(__func__,
                    "vFabric %s Ignoring requested SL setting, as SL is invalid(%d).",
                    vfp->name, vfp->base_sl);
                vfp->base_sl = UNDEFINED_XML8;
            }

            // No BW gets allocated for High Priority VFs, so set to 0,
            //  and this counts as a "configured" BW.
            if (vfp->priority == 1) {
                if (vfp->percent_bandwidth != UNDEFINED_XML8) {
                    IB_LOG_WARN_FMT(__func__,
                        "vFabric %s Ignoring requested BW setting, as VF is high priority.",
                        vfp->name);
                }
                vfp->percent_bandwidth = 0;

                // Count number of high priority VFs
                vfInfo.numHighPriority++;
            }

            // Total configured BW allocations (does not include High Priority VFs)
            if (vfp->percent_bandwidth != UNDEFINED_XML8) {
				if (!vfp->standby) {
					vfInfo.totalConfiguredBandwidth += vfp->percent_bandwidth;
					vfInfo.totalVFwithConfiguredBW++;
				}
            }
        } //end vfp->qos_enable

		//update active list
		int activeVfIdx;
		if (!VirtualFabrics->v_fabric_all[vf].standby) {
			activeVfIdx = findVfIdxInActiveList(&VirtualFabrics->v_fabric_all[vf], VirtualFabrics, TRUE);

			if (activeVfIdx != -1) {
				VirtualFabrics->v_fabric[activeVfIdx].percent_bandwidth = VirtualFabrics->v_fabric_all[vf].percent_bandwidth;
				VirtualFabrics->v_fabric[activeVfIdx].preempt_rank = VirtualFabrics->v_fabric_all[vf].preempt_rank;
				VirtualFabrics->v_fabric[activeVfIdx].flowControlDisable = VirtualFabrics->v_fabric_all[vf].flowControlDisable;
				VirtualFabrics->v_fabric[activeVfIdx].base_sl = VirtualFabrics->v_fabric_all[vf].base_sl;
				VirtualFabrics->v_fabric[activeVfIdx].priority = VirtualFabrics->v_fabric_all[vf].priority;
			}
		}

    } //end loop on all VFs

	//Set activeSLs parameter
	vfInfo.activeSLs = vfInfo.activeVFsQos;

    // Add another "set" of SCs / SLs for the group of "No QOS" VFs.
	vfInfo.totalSLsNeeded = vfInfo.totalVFsQos;
    if (vfInfo.totalVFsNoQos){
        vfInfo.totalSCsNeeded += vfInfo.defaultRoutingSCs;
        vfInfo.totalSLsNeeded++;
		vfInfo.activeSLs++;
    }
}

void sm_process_vf_info(VirtualFabrics_t *VirtualFabrics) {

	//allocate memory used for processing VFs -- only do once, memory cleared on sm_shutdown
	sm_allocate_mem_vfs();

	sm_init_vf_info();

    // In IB each default PKey entry was setup to be a full member.  In STL
    // the default PKey entry is a limited member.
    //
    //setPKey(0, DEFAULT_PKEY | FULL_MEMBER, 0);
    setPKey(0, STL_DEFAULT_APP_PKEY, 0);
    setPKey(1, STL_DEFAULT_PKEY, 0);

    if (!VirtualFabrics || (VirtualFabrics->number_of_vfs == 0))  {
        sm_masterSmSl = 0;
        return;
    }

	sm_process_vf_qos_params(VirtualFabrics);

	boolean qosError = FALSE;
	qosError = sm_error_check_vfs(VirtualFabrics);

	if (qosError) {
		int vf;
        VirtualFabrics->qosEnabled = 0;
        vfInfo.totalVFsQos = 0;
        vfInfo.activeVFsQos = 0;
        vfInfo.totalSLsNeeded = 1;
        vfInfo.totalVFsNoQos = VirtualFabrics->number_of_vfs;
        vfInfo.activeVFsNoQos = VirtualFabrics->number_of_vfs;
        vfInfo.totalSCsNeeded = vfInfo.defaultRoutingSCs;
        vfInfo.totalVFwithConfiguredBW = 0;
        vfInfo.distributedBandwidth = 100;
        vfInfo.numHighPriority = 0;

        for (vf=0; vf<VirtualFabrics->number_of_vfs; vf++) {
            VirtualFabrics->v_fabric[vf].percent_bandwidth = 0;
            VirtualFabrics->v_fabric[vf].qos_enable = 0;
            VirtualFabrics->v_fabric[vf].priority = 0;
            VirtualFabrics->v_fabric[vf].base_sl = 0;
            VirtualFabrics->v_fabric[vf].flowControlDisable = 0;
#ifdef CONFIG_INCLUDE_DOR
            VirtualFabrics->v_fabric[vf].updown_only = 0;
#endif
            VirtualFabrics->v_fabric[vf].preempt_rank = 0;
            VirtualFabrics->v_fabric[vf].routing_scs = vfInfo.defaultRoutingSCs;
            VirtualFabrics->v_fabric[vf].routing_sls = vfInfo.defaultRoutingSCs;
        }

        for (vf=0; vf<VirtualFabrics->number_of_vfs_all; vf++) {
            VirtualFabrics->v_fabric_all[vf].percent_bandwidth = 0;
            VirtualFabrics->v_fabric_all[vf].qos_enable = 0;
            VirtualFabrics->v_fabric_all[vf].priority = 0;
            VirtualFabrics->v_fabric_all[vf].base_sl = 0;
            VirtualFabrics->v_fabric_all[vf].flowControlDisable = 0;
#ifdef CONFIG_INCLUDE_DOR
            VirtualFabrics->v_fabric_all[vf].updown_only = 0;
#endif
            VirtualFabrics->v_fabric_all[vf].preempt_rank = 0;
            VirtualFabrics->v_fabric_all[vf].routing_scs = vfInfo.defaultRoutingSCs;
            VirtualFabrics->v_fabric_all[vf].routing_sls = vfInfo.defaultRoutingSCs;
        }
    }

	sm_assign_base_sls(VirtualFabrics);

	sm_update_bw(VirtualFabrics);

	// setup SL mappings based on new config data
#ifdef USE_FIXED_SCVL_MAPS
	sm_assign_scs_to_sls_FixedMap(VirtualFabrics);
#else
	sm_assign_scs_to_sls(VirtualFabrics);
#endif 

	sm_resolve_pkeys_for_vfs(VirtualFabrics);
}

// Parse the XML configuration
Status_t sm_parse_xml_config(void) {

	uint32_t 	modid;
	uint32_t	adaptiveRoutingDisable = 0;

#ifdef __VXWORKS__
	// if ESM then clean up the globals so we always read data correctly
	sm_cleanGlobals( /* stop */ 0);
#endif

	// The instance for now is the last character in the string sm_0 - sm_3 in the
	// sm_env variable. For now get it out of there so we have an integer instance.
	sm_instance = atoi((char*)&sm_env[3]);
	if (sm_instance >= MAX_INSTANCES) sm_instance = MAX_INSTANCES-1;

#ifndef __VXWORKS__
	// for now it's a fatal error if we can not parse correctly
	xml_config = parseFmConfig(sm_config_filename, IXML_PARSER_FLAG_NONE, sm_instance, /* full parse */ 0, /* embedded */ 0);
	if (!xml_config || !xml_config->fm_instance[sm_instance]) {
		IB_FATAL_ERROR_NODUMP("SM: unable to read configuration file");
		return(VSTATUS_BAD);
	}
#endif // __VXWORKS__

	if (xml_config->xmlDebug.xml_sm_debug) {
		printf("###########sm_env %s sm_instance %u\n", sm_env, (unsigned int)sm_instance);
		xml_trace = 1;
	} else {
		xml_trace = 0;
	}

	// copy the configurations to local structures and adjust accordingly
#ifndef __VXWORKS__
	smCopyConfig(&sm_config,&xml_config->fm_instance[sm_instance]->sm_config);
	pm_config = xml_config->fm_instance[sm_instance]->pm_config;
	fe_config = xml_config->fm_instance[sm_instance]->fe_config;
	sm_dpl_config = xml_config->fm_instance[sm_instance]->sm_dpl_config;
	sm_mc_config = xml_config->fm_instance[sm_instance]->sm_mc_config;
	sm_mls_config = xml_config->fm_instance[sm_instance]->sm_mls_config;
	sm_mdg_config = xml_config->fm_instance[sm_instance]->sm_mdg_config;
#endif

	// use globals for debug since the extent into the MAI subsystem
	sm_debug = sm_config.debug;
	smDebugPerf = sm_config.sm_debug_perf;
	saDebugPerf = sm_config.sa_debug_perf;
	saDebugRmpp = sm_config.debug_rmpp;
	saRmppCheckSum = sm_config.sa_rmpp_checksum;
	smTerminateAfter = sm_config.terminateAfter;
	if (sm_config.dumpCounters[0] != 0) 
		smDumpCounters = sm_config.dumpCounters;

	// if LMC is zero, we will use this offset in the lid allocation logic 
	// value of 1 is the default behavior for LMC=0
	// setting this to 16 would allocate lids in multiples of 16, spreading 
	// the lid range sneaky way to make a small fabric have large lid values

	/* loopTest parameters */
	/* skip the looptest params if looptest was started manually on console */
	if (!esmLoopTestOn) {
		esmLoopTestOn = sm_config.loop_test_on;
		esmLoopTestNumPkts = sm_config.loop_test_packets;
		esmLoopTestFast = sm_config.loop_test_fast_mode;
 		if (esmLoopTestFast == 1) {
 			esmLoopTestInjectEachSweep = 0;
 			esmLoopTestPathLen = 4;
 		}
	}

	if (sm_log_level_override) {
		// command line override
		cs_log_set_log_masks(sm_log_level, sm_config.syslog_mode, sm_log_masks);
	} else {
		sm_log_level = sm_config.log_level;
		for (modid = 0; modid <= VIEO_LAST_MOD_ID; ++modid)
			sm_log_masks[modid] = sm_config.log_masks[modid].value;
	}
	sm_init_log_setting();
	vs_log_output_message("Subnet Manager starting up.", TRUE);

    // Print out XML SM debug settings that could really break SM
    if ((sm_config.forceAttributeRewrite!=0) || (sm_config.skipAttributeWrite!=0)) {
        vs_log_output(VS_LOG_NONE, VIEO_NONE_MOD_ID, NULL, NULL,
                    "SM: (Debug Settings) ForceWrite %d, SkipWriteAttribute Bitmask: 0x%08x",
                     sm_config.forceAttributeRewrite, sm_config.skipAttributeWrite);
    }

#ifndef __VXWORKS__
	vs_init_coredump_settings("SM", sm_config.CoreDumpLimit, sm_config.CoreDumpDir);
#endif // __VXWORKS__

#ifndef __VXWORKS__
#endif

#ifndef __VXWORKS__ // not required for ESM
	// for debugging XML do not deamonize
	if (xml_trace)
		sm_nodaemon = 1;
#endif // __VXWORKS__

	// set global DG value
	sm_def_mc_group = sm_mdg_config.group[0].def_mc_create;

	// get the configuration for Virtual Fabrics
	initialVfPtr = renderVirtualFabricsConfig(sm_instance, xml_config, &sm_config, NULL);

	/* mcast table config */
	sm_init_mcast_mgid_mask_table();

	/* multicast spanning tree root selection parameters*/
	if (strncasecmp(sm_mc_config.mcroot_select_algorithm, "SMNeighbor", 32) == 0) {
		sm_useIdealMcSpanningTreeRoot = 0;
		sm_mcSpanningTreeRoot_useLeastTotalCost = 0;
		sm_mcSpanningTreeRoot_useLeastWorstCaseCost = 0;
	} else if (strncasecmp(sm_mc_config.mcroot_select_algorithm, "LeastTotalCost", 32) == 0) {
		sm_useIdealMcSpanningTreeRoot = 1;
		sm_mcSpanningTreeRoot_useLeastTotalCost = 1;
		sm_mcSpanningTreeRoot_useLeastWorstCaseCost = 0;
	}  else if (strncasecmp(sm_mc_config.mcroot_select_algorithm, "LeastWorstCaseCost", 32) == 0) {
		sm_useIdealMcSpanningTreeRoot = 1;
		sm_mcSpanningTreeRoot_useLeastWorstCaseCost = 1;
		sm_mcSpanningTreeRoot_useLeastTotalCost = 0;
	} else {
		if (sm_mc_config.mcroot_select_algorithm[0] != 0)
			IB_LOG_WARN_FMT(__func__,
			       "Invalid Multicast Root Selection algorithm ('%s'); defaulting to 'LeastTotalCost'",
			       sm_mc_config.mcroot_select_algorithm);
		sm_useIdealMcSpanningTreeRoot = 1;
		sm_mcSpanningTreeRoot_useLeastTotalCost = 1;
		sm_mcSpanningTreeRoot_useLeastWorstCaseCost = 0;
	}

	/* multicast spanning tree root update parameters */
	if (strlen(sm_mc_config.mcroot_min_cost_improvement) > 0) {
		char * percent;
		percent = strstr(sm_mc_config.mcroot_min_cost_improvement, "%");
		if (!percent) {
			IB_LOG_WARN_FMT(__func__,
			       "Multicast Root MinCostImprovement Parameter must be in percentage form - example 50%%."
					" Defaulting to %d%%", DEFAULT_MCROOT_COST_IMPROVEMENT_PERCENTAGE);
			sm_mcRootCostDeltaThreshold = DEFAULT_MCROOT_COST_IMPROVEMENT_PERCENTAGE;
		} else {
			*percent = 0;
			sm_mcRootCostDeltaThreshold = atoi(sm_mc_config.mcroot_min_cost_improvement);
		}
	} else {
		sm_mcRootCostDeltaThreshold = DEFAULT_MCROOT_COST_IMPROVEMENT_PERCENTAGE;
	}

	/* get the master ping interval params used by stanby SMs */
	sm_masterCheckInterval = (uint64_t)sm_config.master_ping_interval * VTIMER_1S;  /* put in in microsecs */
	if (sm_masterCheckInterval == 0)
		sm_masterCheckInterval = SM_CHECK_MASTER_INTERVAL * VTIMER_1S;

	/* check the trap threshold for auto-disabling ports throwing errors (0=off) */
	if (sm_config.trap_threshold != 0 &&
		(sm_config.trap_threshold < SM_TRAP_THRESHOLD_MIN || sm_config.trap_threshold > SM_TRAP_THRESHOLD_MAX)) {
		IB_LOG_WARN_FMT(__func__, "trap threshold of %d is out of range %d-%d, defaulting to %d",
			sm_config.trap_threshold, SM_TRAP_THRESHOLD_MIN, SM_TRAP_THRESHOLD_MAX, SM_TRAP_THRESHOLD_DEFAULT);
		sm_config.trap_threshold = SM_TRAP_THRESHOLD_DEFAULT;
	}

	if (sm_config.trap_threshold_min_count != 0 &&
		(sm_config.trap_threshold_min_count < SM_TRAP_THRESHOLD_COUNT_MIN)) {
		IB_LOG_WARN_FMT(__func__, "MinTrapThresholdCount of %d is invalid. Must be greater than or equal to %d. Defaulting to %d.",
			sm_config.trap_threshold_min_count, SM_TRAP_THRESHOLD_COUNT_MIN, SM_TRAP_THRESHOLD_COUNT_DEFAULT);
		sm_config.trap_threshold_min_count = SM_TRAP_THRESHOLD_COUNT_DEFAULT;
	}

	sm_setTrapThreshold(sm_config.trap_threshold, sm_config.trap_threshold_min_count);

	sm_mcDosThreshold = sm_config.mc_dos_threshold;
	if (sm_mcDosThreshold > SM_TRAP_THRESHOLD_MAX) {
		IB_LOG_WARN_FMT(__func__, "McDosThreshold of %d is invalid. Exceeds max of %d. Defaulting to max.",
						sm_mcDosThreshold, SM_TRAP_THRESHOLD_MAX);
		sm_mcDosThreshold = SM_TRAP_THRESHOLD_MAX;
	}
	sm_mcDosAction = sm_config.mc_dos_action;
	sm_mcDosInterval = sm_config.mc_dos_interval * VTIMER_1S;

#ifdef IB_STACK_OPENIB
	if (sm_config.debug_jm) {
		IB_LOG_INFINI_INFO0("SM: Enabling Job Management debug");
		ib_instrumentJmMads = 1;
	}
#endif

	// do we need to disable adaptive routing if loop test is on
	if (esmLoopTestOn && !esmLoopTestAllowAdaptiveRouting)
		adaptiveRoutingDisable = 1;
	else
		adaptiveRoutingDisable = 0;
	
	// Is Adaptive Routing Enabled
	if (sm_config.adaptiveRouting.enable && !adaptiveRoutingDisable) {
		sm_adaptiveRouting.enable = sm_config.adaptiveRouting.enable;
		sm_adaptiveRouting.debug = sm_config.adaptiveRouting.debug;
		sm_adaptiveRouting.algorithm = sm_config.adaptiveRouting.algorithm;
		sm_adaptiveRouting.lostRouteOnly = sm_config.adaptiveRouting.lostRouteOnly;
		sm_adaptiveRouting.arFrequency = sm_config.adaptiveRouting.arFrequency;
		sm_adaptiveRouting.threshold = sm_config.adaptiveRouting.threshold;

	} else {
		if (sm_config.adaptiveRouting.enable) {
			// Can be enabled when loop test stops
			sm_looptest_disabled_ar = 1;
			IB_LOG_WARN0("SM: Adaptive Routing disabled during loop test");
		}

		memset(&sm_adaptiveRouting, 0, sizeof(SmAdaptiveRouting_t));
	}

	if (xml_trace) {
		smShowConfig(&sm_config, &sm_dpl_config, &sm_mc_config, &sm_mls_config);
	}

	if (xml_trace) {
		printf("XML - SM old overall_checksum %llu new overall_checksum %llu\n",
			(long long unsigned int)sm_overall_checksum, (long long unsigned int)sm_config.overall_checksum);
		printf("XML - SM old consistency_checksum %llu new consistency_checksum %llu\n",
			(long long unsigned int)sm_consistency_checksum, (long long unsigned int)sm_config.consistency_checksum);
	}
	sm_overall_checksum = sm_config.overall_checksum;
	sm_consistency_checksum = sm_config.consistency_checksum;

	//loop on all DGs and convert all node descriptions
	int dgIdx;
	DGConfig_t *dgp;
	int numGroups = xml_config->fm_instance[sm_instance]->dg_config.number_of_dgs;
	for (dgIdx=0; dgIdx<numGroups; ++dgIdx) {

		dgp = xml_config->fm_instance[sm_instance]->dg_config.dg[dgIdx];

		if (dgp != NULL) {

			//loop on node descriptions
			if (dgp->number_of_node_descriptions > 0) {

				XmlNode_t* nodeDescPtr = dgp->node_description;
				RegExp_t*  regExprPtr = dgp->reg_expr;

				while ( (nodeDescPtr != NULL)&&(regExprPtr != NULL) ) {

					initializeRegexStruct(regExprPtr);					

					boolean isValid = FALSE;

					isValid = convertWildcardedString(nodeDescPtr->node, &regExprPtr->regexString[0], &regExprPtr->regexInfo);

					if (!isValid) {
						IB_LOG_WARN_FMT(__func__, "convertWildcardedString returned syntax invalid for node description: %s", nodeDescPtr->node);
					}
					else {

#ifdef __VXWORKS__
						RegexBracketParseInfo_t* regexInfoPtr = &regExprPtr->regexInfo;
						if (regexInfoPtr->numBracketRangesDefined > 0) {
							IB_LOG_WARN_FMT(__func__, "DG evaluation ignoring NodeDesc %s: VxWorks does not support bracket syntax in node descriptions", nodeDescPtr->node);
							isValid = FALSE;
						}
						else {
							if ((regExprPtr->regexpCompiled = regcomp(&regExprPtr->regexString[0])) == NULL) {
							IB_LOG_WARN_FMT(__func__, "Could not compile regular expression: %s", &regExprPtr->regexString[0]);
								isValid = FALSE;
						}
						else {
							isValid = TRUE;
						}
						}
#else
						//compile into regular expression if there aren't any syntax issues
						int regCompRetVal = regcomp(&regExprPtr->regexCompiled, &regExprPtr->regexString[0], REG_EXTENDED);

						if (regCompRetVal != 0) {
							char errorMsg[1000];
							regerror(regCompRetVal, &regExprPtr->regexCompiled, errorMsg, sizeof(errorMsg));
							IB_LOG_WARN_FMT(__func__, "Could not compile regular expression: %s: ErrorCode: %d Detail: %s", &regExprPtr->regexString[0], regCompRetVal, errorMsg);
							isValid = FALSE;
						}
					else {
							isValid = TRUE;
					}
#endif
					}

					//save isValid flag
					regExprPtr->isSyntaxValid = isValid;

					nodeDescPtr = nodeDescPtr->next;
					regExprPtr = regExprPtr->next;
				}
			}
		}
	}

	return VSTATUS_OK; 
}

void sm_compute_pool_size(void)
{
#ifdef __VXWORKS__
	// VxWorks computes smPoolSize in EsmInit.c based on memory available
#else
	// this doesn't really matter on linux
	g_smPoolSize = 0x10000000;
#endif
}
		
#ifndef __VXWORKS__
// initialize the memory pool for XML parsing
Status_t 
initSmXmlMemoryPool(void) {

	Status_t	status;
	uint32_t sm_xml_bytes;

	sm_xml_bytes = xml_compute_pool_size(/* one instance of sm */ 0);

    memset(&sm_xml_pool, 0, sizeof(sm_xml_pool));
	status = vs_pool_create(&sm_xml_pool, 0, (uint8_t *)"sm_xml_pool", NULL, sm_xml_bytes);
	if (status != VSTATUS_OK) {
		IB_FATAL_ERROR("sm_main: can't create SM XML pool, ABORTING SM START");
        memset(&sm_xml_pool, 0, sizeof(sm_xml_pool));
        return status;
	}
	return VSTATUS_OK; 
}

// get memory for XML parser
void* 
getSmXmlParserMemory(uint32_t size, char* info) {
	void 		*address;
	Status_t    status;

#ifdef XML_MEMORY
	printf("called getSmXmlParserMemory() size (%u) (%s) from sm_main.c\n", size, info);
#endif
	status = vs_pool_alloc(&sm_xml_pool, size, (void*)&address);
	if (status != VSTATUS_OK || !address)
		return NULL;
	return address;
}

Status_t 
handleVfDgMemory(void) {

	Status_t status = VSTATUS_OK;

	//allocate memory for arrays inside vf_config, dg_config
	int idx;
	for (idx = 0; idx<xml_config->fm_instance[sm_instance]->vf_config.number_of_vfs; idx++) {
		status = vs_pool_alloc(&sm_pool, sizeof(VFConfig_t), (void*)&vf_config.vf[idx]);
		if (status != VSTATUS_OK || !vf_config.vf[idx]) {
			IB_LOG_ERROR0("can't malloc vf_config.vf");
			return status;
		}
	}

	for (idx = 0; idx<xml_config->fm_instance[sm_instance]->dg_config.number_of_dgs; idx++) {
		status = vs_pool_alloc(&sm_pool, sizeof(VFConfig_t), (void*)&dg_config.dg[idx]);
		if (status != VSTATUS_OK || !dg_config.dg[idx]) {
			IB_LOG_ERROR0("can't malloc dg_config.dg");
			return status;
		}
	}

	//copy xml_config memory to dg_config, and vf_config
	if (copyDgVfInfo(xml_config->fm_instance[sm_instance], &dg_config, &vf_config)) {
		IB_FATAL_ERROR("can't copy VF DG configuration");
		status = VSTATUS_NOMEM;
	}

	return status;
}

// free memory for XML parser
void 
freeSmXmlParserMemory(void *address, uint32_t size, char* info) {

#ifdef XML_MEMORY
	printf("called freeSmXmlParserMemory() size (%u) (%s) from sm_main.c\n", size, info);
#endif
	vs_pool_free(&sm_xml_pool, address);

}

Status_t
sm_initialize_sm_pool(void) {

	Status_t status = VSTATUS_OK;

	sm_compute_pool_size();

    memset(&sm_pool, 0, sizeof(sm_pool));
	status = vs_pool_create(&sm_pool, 0, (uint8_t *)"sm_pool", NULL, g_smPoolSize);
	if (status != VSTATUS_OK) {
		IB_FATAL_ERROR("can't create SM pool, ABORTING SM START");
        memset(&sm_pool,0,sizeof(sm_pool));
        return status;
	}

	return status;
}

#endif // __VXWORKS__

void sm_test_logging_macros(void) {
#if 0
	// some tests of various log routines and macros
	IB_LOG_ERROR0("Test IB_LOG_ERROR0");
	IB_LOG_ERROR("Test IB_LOG_ERROR with 55 as value", 55);
	IB_LOG_ERRORX("Test IB_LOG_ERROR with 33 as value", 33);
	IB_LOG_ERRORLX("Test IB_LOG_ERROR with 0x1234567890abcdef as value", 0x1234567890abcdefULL);
	IB_LOG_ERROR64("Test IB_LOG_ERROR with 1234567812345678 as value", 12345678123456789ULL);
	IB_LOG_ERRORSTR("Test IB_LOG_ERROR with string as value", "string");
	IB_LOG_ERRORRC("Test IB_LOG_ERROR with rc as value", VSTATUS_TIMEOUT);
	IB_LOG_WARN0("Test IB_LOG_WARN0");
	IB_LOG_WARN("Test IB_LOG_WARN with 55 as value", 55);
	IB_LOG_WARNX("Test IB_LOG_WARN with 33 as value", 33);
	IB_LOG_WARNLX("Test IB_LOG_WARN with 0x1234567890abcdef as value", 0x1234567890abcdefULL);
	IB_LOG_WARN64("Test IB_LOG_WARN with 1234567812345678 as value", 12345678123456789ULL);
	IB_LOG_WARNSTR("Test IB_LOG_WARN with string as value", "string");
	IB_LOG_WARNRC("Test IB_LOG_WARN with rc as value", VSTATUS_TIMEOUT);
	IB_LOG_NOTICE0("Test IB_LOG_NOTICE0");
	IB_LOG_NOTICE("Test IB_LOG_NOTICE with 55 as value", 55);
	IB_LOG_NOTICEX("Test IB_LOG_NOTICE with 33 as value", 33);
	IB_LOG_NOTICELX("Test IB_LOG_NOTICE with 0x1234567890abcdef as value", 0x1234567890abcdefULL);
	IB_LOG_NOTICE64("Test IB_LOG_NOTICE with 1234567812345678 as value", 12345678123456789ULL);
	IB_LOG_NOTICESTR("Test IB_LOG_NOTICE with string as value", "string");
	IB_LOG_NOTICERC("Test IB_LOG_NOTICE with rc as value", VSTATUS_TIMEOUT);
	IB_LOG_INFINI_INFO0("Test IB_LOG_INFINI_INFO0");
	IB_LOG_INFINI_INFO("Test IB_LOG_INFINI_INFO with 55 as value", 55);
	IB_LOG_INFINI_INFOX("Test IB_LOG_INFINI_INFO with 33 as value", 33);
	IB_LOG_INFINI_INFOLX("Test IB_LOG_INFINI_INFO with 0x1234567890abcdef as value", 0x1234567890abcdefULL);
	IB_LOG_INFINI_INFO64("Test IB_LOG_INFINI_INFO with 1234567812345678 as value", 12345678123456789ULL);
	IB_LOG_INFINI_INFOSTR("Test IB_LOG_INFINI_INFO with string as value", "string");
	IB_LOG_INFINI_INFORC("Test IB_LOG_INFINI_INFO with rc as value", VSTATUS_TIMEOUT);
	IB_LOG_INFO0("Test IB_LOG_INFO0");
	IB_LOG_INFO("Test IB_LOG_INFO with 55 as value", 55);
	IB_LOG_INFOX("Test IB_LOG_INFO with 33 as value", 33);
	IB_LOG_INFOLX("Test IB_LOG_INFO with 0x1234567890abcdef as value", 0x1234567890abcdefULL);
	IB_LOG_INFO64("Test IB_LOG_INFO with 1234567812345678 as value", 12345678123456789ULL);
	IB_LOG_INFOSTR("Test IB_LOG_INFO with string as value", "string");
	IB_LOG_INFORC("Test IB_LOG_INFO with rc as value", VSTATUS_TIMEOUT);
	IB_LOG_VERBOSE0("Test IB_LOG_VERBOSE0");
	IB_LOG_VERBOSE("Test IB_LOG_VERBOSE with 55 as value", 55);
	IB_LOG_VERBOSEX("Test IB_LOG_VERBOSE with 33 as value", 33);
	IB_LOG_VERBOSELX("Test IB_LOG_VERBOSE with 0x1234567890abcdef as value", 0x1234567890abcdefULL);
	IB_LOG_VERBOSE64("Test IB_LOG_VERBOSE with 1234567812345678 as value", 12345678123456789ULL);
	IB_LOG_VERBOSESTR("Test IB_LOG_VERBOSE with string as value", "string");
	IB_LOG_VERBOSERC("Test IB_LOG_VERBOSE with rc as value", VSTATUS_TIMEOUT);
	IB_LOG_DATA("Test IB_LOG_DATA with str","Memory will be dumped in hex for easy reading", 45);
	IB_LOG_DEBUG1_0("Test IB_LOG_DEBUG1_0");
	IB_LOG_DEBUG1("Test IB_LOG_DEBUG1 with 55 as value", 55);
	IB_LOG_DEBUG1X("Test IB_LOG_DEBUG1 with 33 as value", 33);
	IB_LOG_DEBUG1LX("Test IB_LOG_DEBUG1 with 0x1234567890abcdef as value", 0x1234567890abcdefULL);
	IB_LOG_DEBUG1_64("Test IB_LOG_DEBUG1 with 1234567812345678 as value", 12345678123456789ULL);
	IB_LOG_DEBUG1STR("Test IB_LOG_DEBUG1 with string as value", "string");
	IB_LOG_DEBUG1RC("Test IB_LOG_DEBUG1 with rc as value", VSTATUS_TIMEOUT);
	IB_LOG_DEBUG2_0("Test IB_LOG_DEBUG2_0");
	IB_LOG_DEBUG2("Test IB_LOG_DEBUG2 with 55 as value", 55);
	IB_LOG_DEBUG2X("Test IB_LOG_DEBUG2 with 33 as value", 33);
	IB_LOG_DEBUG2LX("Test IB_LOG_DEBUG2 with 0x1234567890abcdef as value", 0x1234567890abcdefULL);
	IB_LOG_DEBUG2_64("Test IB_LOG_DEBUG2 with 1234567812345678 as value", 12345678123456789ULL);
	IB_LOG_DEBUG2STR("Test IB_LOG_DEBUG2 with string as value", "string");
	IB_LOG_DEBUG2RC("Test IB_LOG_DEBUG2 with rc as value", VSTATUS_TIMEOUT);
	IB_LOG_DEBUG3_0("Test IB_LOG_DEBUG3_0");
	IB_LOG_DEBUG3("Test IB_LOG_DEBUG3 with 55 as value", 55);
	IB_LOG_DEBUG3X("Test IB_LOG_DEBUG3 with 33 as value", 33);
	IB_LOG_DEBUG3LX("Test IB_LOG_DEBUG3 with 0x1234567890abcdef as value", 0x1234567890abcdefULL);
	IB_LOG_DEBUG3_64("Test IB_LOG_DEBUG3 with 1234567812345678 as value", 12345678123456789ULL);
	IB_LOG_DEBUG3STR("Test IB_LOG_DEBUG3 with string as value", "string");
	IB_LOG_DEBUG3RC("Test IB_LOG_DEBUG3 with rc as value", VSTATUS_TIMEOUT);
	IB_LOG_DEBUG4_0("Test IB_LOG_DEBUG4_0");
	IB_LOG_DEBUG4("Test IB_LOG_DEBUG4 with 55 as value", 55);
	IB_LOG_DEBUG4X("Test IB_LOG_DEBUG4 with 33 as value", 33);
	IB_LOG_DEBUG4LX("Test IB_LOG_DEBUG4 with 0x1234567890abcdef as value", 0x1234567890abcdefULL);
	IB_LOG_DEBUG4_64("Test IB_LOG_DEBUG4 with 1234567812345678 as value", 12345678123456789ULL);
	IB_LOG_DEBUG4STR("Test IB_LOG_DEBUG4 with string as value", "string");
	IB_LOG_DEBUG4RC("Test IB_LOG_DEBUG4 with rc as value", VSTATUS_TIMEOUT);
	IB_ENTER(__func__,4);
	IB_LOG_ARGS1(1);
	IB_LOG_ARGS2(1,2);
	IB_LOG_ARGS3(1,2,3);
	IB_LOG_ARGS4(1,2,3,4);
	IB_LOG_ARGS5(1,2,3,4,5);
	IB_EXIT0("Test IB_EXIT with no value");
	IB_EXIT(__func__, 55);
	cs_log(VS_LOG_ERROR, "test_func", "Test cs_log VS_LOG_ERROR with 55, 6 as values %u, %u", 55, 6);
	IB_LOG_ERROR_FMT(__func__, "Test IB_LOG_ERROR_FMT with 55, 6 as values %u, %u", 55, 6);
	cs_log(VS_LOG_WARN, "test_func", "Test cs_log VS_LOG_WARN with 55, 6 as values %u, %u", 55, 6);
	IB_LOG_WARN_FMT(__func__, "Test IB_LOG_WARN_FMT with 55, 6 as values %u, %u", 55, 6);
	cs_log(VS_LOG_NOTICE, "test_func", "Test cs_log VS_LOG_NOTICE with 55, 6 as values %u, %u", 55, 6);
	IB_LOG_NOTICE_FMT(__func__, "Test IB_LOG_NOTICE_FMT with 55, 6 as values %u, %u", 55, 6);
	cs_log(VS_LOG_INFINI_INFO, "test_func", "Test cs_log VS_LOG_INFINI_INFO with 55, 6 as values %u, %u", 55, 6);
	IB_LOG_INFINI_INFO_FMT(__func__, "Test IB_LOG_INFINI_INFO_FMT with 55, 6 as values %u, %u", 55, 6);
	cs_log(VS_LOG_INFO, "test_func", "Test cs_log VS_LOG_INFO with 55, 6 as values %u, %u", 55, 6);
	IB_LOG_INFO_FMT(__func__, "Test IB_LOG_INFO_FMT with 55, 6 as values %u, %u", 55, 6);
	cs_log(VS_LOG_VERBOSE, "test_func", "Test cs_log VS_LOG_VERBOSE with 55, 6 as values %u, %u", 55, 6);
	IB_LOG_VERBOSE_FMT(__func__, "Test IB_LOG_VERBOSE_FMT with 55, 6 as values %u, %u", 55, 6);
	cs_log(VS_LOG_DATA, "test_func", "Test cs_log VS_LOG_DATA with 55, 6 as values %u, %u", 55, 6);
	cs_log(VS_LOG_DEBUG1, "test_func", "Test cs_log VS_LOG_DEBUG1 with 55, 6 as values %u, %u", 55, 6);
	IB_LOG_DEBUG1_FMT(__func__, "Test IB_LOG_DEBUG1_FMT with 55, 6 as values %u, %u", 55, 6);
	cs_log(VS_LOG_DEBUG2, "test_func", "Test cs_log VS_LOG_DEBUG2 with 55, 6 as values %u, %u", 55, 6);
	IB_LOG_DEBUG2_FMT(__func__, "Test IB_LOG_DEBUG2_FMT with 55, 6 as values %u, %u", 55, 6);
	cs_log(VS_LOG_DEBUG3, "test_func", "Test cs_log VS_LOG_DEBUG3 with 55, 6 as values %u, %u", 55, 6);
	IB_LOG_DEBUG3_FMT(__func__, "Test IB_LOG_DEBUG3_FMT with 55, 6 as values %u, %u", 55, 6);
	cs_log(VS_LOG_DEBUG4, "test_func", "Test cs_log VS_LOG_DEBUG4 with 55, 6 as values %u, %u", 55, 6);
	IB_LOG_DEBUG4_FMT(__func__, "Test IB_LOG_DEBUG4_FMT with 55, 6 as values %u, %u", 55, 6);
	cs_log(VS_LOG_ENTER, "test_func", "Test cs_log VS_LOG_ENTER with 55, 6 as values %u, %u", 55, 6);
	cs_log(VS_LOG_ARGS, "test_func", "Test cs_log VS_LOG_ARGS with 55, 6 as values %u, %u", 55, 6);
	cs_log(VS_LOG_EXIT, "test_func", "Test cs_log VS_LOG_EXIT with 55, 6 as values %u, %u", 55, 6);
	cs_log_vf(VS_LOG_ERROR, "test_vf", "test_func", "Test cs_log_vf VS_LOG_ERROR with 55, 6 as values %u, %u", 55, 6);
	IB_LOG_ERROR_FMT_VF("test_vf","test_func", "Test IB_LOG_ERROR_FMT_VF with 55, 6 as values %u, %u", 55, 6);
	cs_log_vf(VS_LOG_WARN, "test_vf", "test_func", "Test cs_log_vf VS_LOG_WARN with 55, 6 as values %u, %u", 55, 6);
	IB_LOG_WARN_FMT_VF("test_vf", "test_func", "Test IB_LOG_WARN_FMT_VF with 55, 6 as values %u, %u", 55, 6);
	cs_log_vf(VS_LOG_NOTICE, "test_vf", "test_func", "Test cs_log_vf VS_LOG_NOTICE with 55, 6 as values %u, %u", 55, 6);
	IB_LOG_NOTICE_FMT_VF("test_vf", "test_func", "Test IB_LOG_NOTICE_FMT_VF with 55, 6 as values %u, %u", 55, 6);
	cs_log_vf(VS_LOG_INFINI_INFO, "test_vf", "test_func", "Test cs_log_vf VS_LOG_INFINI_INFO with 55, 6 as values %u, %u", 55, 6);
	IB_LOG_INFINI_INFO_FMT_VF("test_vf", "test_func", "Test IB_LOG_INFINI_INFO_FMT_VF with 55, 6 as values %u, %u", 55, 6);
	cs_log_vf(VS_LOG_INFO, "test_vf", "test_func", "Test cs_log_vf VS_LOG_INFO with 55, 6 as values %u, %u", 55, 6);
	IB_LOG_INFO_FMT_VF("test_vf", "test_func", "Test IB_LOG_INFO_FMT_VF with 55, 6 as values %u, %u", 55, 6);
	cs_log_vf(VS_LOG_VERBOSE, "test_vf", "test_func", "Test cs_log_vf VS_LOG_VERBOSE with 55, 6 as values %u, %u", 55, 6);
	IB_LOG_VERBOSE_FMT_VF("test_vf", "test_func", "Test IB_LOG_VERBOSE_FMT_VF with 55, 6 as values %u, %u", 55, 6);
	cs_log_vf(VS_LOG_DATA, "test_vf", "test_func", "Test cs_log_vf VS_LOG_DATA with 55, 6 as values %u, %u", 55, 6);
	cs_log_vf(VS_LOG_DEBUG1, "test_vf", "test_func", "Test cs_log_vf VS_LOG_DEBUG1 with 55, 6 as values %u, %u", 55, 6);
	IB_LOG_DEBUG1_FMT_VF("test_vf", "test_func", "Test IB_LOG_DEBUG1_FMT_VF with 55, 6 as values %u, %u", 55, 6);
	cs_log_vf(VS_LOG_DEBUG2, "test_vf", "test_func", "Test cs_log_vf VS_LOG_DEBUG2 with 55, 6 as values %u, %u", 55, 6);
	IB_LOG_DEBUG2_FMT_VF("test_vf", "test_func", "Test IB_LOG_DEBUG2_FMT_VF with 55, 6 as values %u, %u", 55, 6);
	cs_log_vf(VS_LOG_DEBUG3, "test_vf", "test_func", "Test cs_log_vf VS_LOG_DEBUG3 with 55, 6 as values %u, %u", 55, 6);
	IB_LOG_DEBUG3_FMT_VF("test_vf", "test_func", "Test IB_LOG_DEBUG3_FMT_VF with 55, 6 as values %u, %u", 55, 6);
	cs_log_vf(VS_LOG_DEBUG4, "test_vf", "test_func", "Test cs_log_vf VS_LOG_DEBUG4 with 55, 6 as values %u, %u", 55, 6);
	IB_LOG_DEBUG4_FMT_VF("test_vf", "test_func", "Test IB_LOG_DEBUG4_FMT_VF with 55, 6 as values %u, %u", 55, 6);
	cs_log_vf(VS_LOG_ENTER, "test_vf", "test_func", "Test cs_log_vf VS_LOG_ENTER with 55, 6 as values %u, %u", 55, 6);
	cs_log_vf(VS_LOG_ARGS, "test_vf", "test_func", "Test cs_log_vf VS_LOG_ARGS with 55, 6 as values %u, %u", 55, 6);
	cs_log_vf(VS_LOG_EXIT, "test_vf", "test_func", "Test cs_log_vf VS_LOG_EXIT with 55, 6 as values %u, %u", 55, 6);
	smCsmLogMessage(CSM_SEV_ERROR, CSM_COND_SECURITY_ERROR, NULL, NULL, "Test CSM ERROR message with 55, 6 as values %u, %u", 55, 6);
	smCsmLogMessage(CSM_SEV_WARNING, CSM_COND_SECURITY_ERROR, NULL, NULL, "Test CSM WARNING message with 55, 6 as values %u, %u", 55, 6);
	smCsmLogMessage(CSM_SEV_NOTICE, CSM_COND_SECURITY_ERROR, NULL, NULL, "Test CSM NOTICE message with 55, 6 as values %u, %u", 55, 6);
	smCsmLogMessage(CSM_SEV_INFO, CSM_COND_SECURITY_ERROR, NULL, NULL, "Test CSM INFO message with 55, 6 as values %u, %u", 55, 6);
	//IB_FATAL_ERROR("Test IB_LOG_FATAL");
	log_mask &= ~(VS_LOG_TRACE);	// reduce logging for rest of run
#endif
}

Status_t
sm_main(void) {
	Status_t	status;
#ifndef __VXWORKS__
	int         startPM=0;
#ifdef FE_THREAD_SUPPORT_ENABLED
	int         startFE=0;
#endif
#endif

	IB_ENTER(__func__, 0, 0, 0, 0);

//
//	Check for authorization and licenses.
//
	if (authorization_userexit() != VSTATUS_OK) {
		IB_FATAL_ERROR("authorization failed");
	}

	if (license_userexit() != VSTATUS_OK) {
		IB_FATAL_ERROR("license failed");
	}

    (void) sm_spanning_tree_resetGlobals();

//
//	Fetch the environment.
//
#ifndef __VXWORKS__

	(void)read_info_file();

    // get PM related XML configuration parameters so we know if it should
	// be started
    status = pm_initialize_config();
	if (status != VSTATUS_OK) {
		IB_FATAL_ERROR("can't retrieve PM XML configuration");
		return status;
    }
    startPM = pm_config.start;

    // get FE related XML configuration parameters so we know if it should
	// be started
    status = fe_initialize_config(xml_config, sm_instance);
	if (status != VSTATUS_OK) {
		IB_FATAL_ERROR("can't retrieve FE XML configuration");
		return status;
    }

#ifdef FE_THREAD_SUPPORT_ENABLED
    startFE = fe_config.start;
#endif

#else // __VXWORKS__
	// Parse the XML configuration
	status = sm_parse_xml_config();
	if (status != VSTATUS_OK) {
		return status;
	}

	if (copyDgVfInfo(xml_config->fm_instance[sm_instance], &dg_config, &vf_config)) {
		IB_FATAL_ERROR("can't copy VF DG configuration");
		return VSTATUS_NOMEM;
	}

#endif // __VXWORKS__

    sm_init_plt_table();

#ifndef __VXWORKS__
	// since the XML VirtualFabrics configuration has been rendered the memory
	// used for parsing XML and some of the common memory can be released
	releaseXmlConfig(xml_config, /* full */ 1);
	xml_config = NULL;
#endif

	sm_test_logging_macros();
    /* dbsync interval used internally in seconds */
    sm_config.db_sync_interval = ((sm_config.db_sync_interval > 60) ? (60*60) : (sm_config.db_sync_interval*60));
	sa_max_cntxt = 2 * sm_config.subnet_size;

	// the same value for embedded, but grows nicely for Host SM
	sa_data_length = 512 * cs_numPortRecords(sm_config.subnet_size);
	sa_max_path_records = sa_data_length / (sizeof(IB_PATH_RECORD) + Calculate_Padding(sizeof(IB_PATH_RECORD)));

    sm_mkey_protect_level = (sm_config.mkey) ? sm_default_mkey_protect_level : 0;
    sm_mkey_lease_period = (sm_config.mkey) ? sm_config.timer : 0;  /* this is in seconds */
	{
		char buf[200];

    	sprintf(buf, 
           "SM: GidPrefix="FMT_U64", Key="FMT_U64", "
           "MKey="FMT_U64" : protect_level=%d : lease=%d seconds, dbsync interval=%d seconds",
           sm_config.subnet_prefix, sm_config.sm_key, sm_config.mkey, sm_default_mkey_protect_level, 
		   sm_mkey_lease_period, (unsigned int)sm_config.db_sync_interval);
		vs_log_output_message(buf, FALSE);
	}
	sm_config.timer *= 1000000;
	sm_lid = sm_config.lid;

    /* 
     * if LMC is zero, we will use this offset in the lid allocation logic 
     * value of 1 is the default behavior for LMC=0
     * setting this to 16 would allocate lids in multiples of 16, spreading the lid range
     * sneaky way to make a small fabric have large lid values
     */
    if (sm_config.topo_lid_offset < 1 || sm_config.topo_lid_offset > 256) {
        sm_config.topo_lid_offset = 1;
    } else if (sm_config.topo_lid_offset > 1) {
        IB_LOG_INFINI_INFO("SM will allocate lids in increments of ", sm_config.topo_lid_offset);
    }

#ifndef __VXWORKS__

	if (!sm_nodaemon) {
		int	ret_value;
		IB_LOG_INFO("Trying daemon, sm_nodaemon =", sm_nodaemon);
		if ((ret_value = daemon(1, 0))) {
			int localerrno = errno;
			IB_LOG_ERROR("daemon failed with return value of", ret_value);
			IB_LOG_ERROR(strerror(localerrno), localerrno);
		}
	}

#endif

//
//	Initialize the pools.
//
	{
		char buf[140];

		sprintf(buf, "SM: Size Limits: EndNodePorts=%u Nodes=%u Ports=%u Links=%u",
				(unsigned)sm_config.subnet_size,
				(unsigned)cs_numNodeRecords(sm_config.subnet_size),
				(unsigned)cs_numPortRecords(sm_config.subnet_size),
				(unsigned)cs_numLinkRecords(sm_config.subnet_size));
		vs_log_output_message(buf, FALSE);

		sprintf(buf, "SM: Memory: Pool=%uK SA Resp=%uK",
				(unsigned)(g_smPoolSize+1023)/1024,
				(unsigned)(sa_data_length+1023)/1024);
		vs_log_output_message(buf, FALSE);
	}

	lidmap = NULL;
	status = vs_pool_alloc(&sm_pool, sizeof(LidMap_t) * (UNICAST_LID_MAX + 1), (void*)&lidmap);
	if (status != VSTATUS_OK || !lidmap) {
		status = VSTATUS_NOMEM;
		return status;
	}
	memset(lidmap, 0, sizeof(LidMap_t) * (UNICAST_LID_MAX + 1));

	status = vs_pool_alloc(&sm_pool, sizeof(cl_qmap_t), (void *)&sm_GuidToLidMap);
	if (status != VSTATUS_OK || !sm_GuidToLidMap) {
		IB_LOG_ERROR0("can't malloc GuidToLidMap");
		status = VSTATUS_NOMEM;
		return status;
	}
	cl_qmap_init(sm_GuidToLidMap, NULL);

	sm_lmc_e0_freeLid_hint = 1 << sm_config.lmc_e0;
	sm_lmc_freeLid_hint = 1 << sm_config.lmc;
	sm_lmc_0_freeLid_hint = 1;

	sm_threads = NULL;
	status = vs_pool_alloc(&sm_pool, sizeof(SMThread_t) * (SM_THREAD_MAX + 1), (void*)&sm_threads);
	if (status != VSTATUS_OK || !sm_threads) {
		status = VSTATUS_NOMEM;
		return status;
	}

	uniqueSpanningTrees = NULL;
	status = vs_pool_alloc(&sm_pool, sizeof(McSpanningTree_t *) * (STL_MTU_MAX * IB_STATIC_RATE_MAX), (void*)&uniqueSpanningTrees);
	if (status != VSTATUS_OK || !uniqueSpanningTrees) {
		IB_LOG_ERROR0("can't allocate uniqueSpanningTrees array from SM memory pool");
		status = VSTATUS_NOMEM;
		return status;
	}

	sm_process_vf_info(initialVfPtr);

	//Set VirtualFabrics* in the topology_t structure
	old_topology.vfs_ptr = initialVfPtr;


//
//	Initialize the semaphores.
//
	if ((status = cs_sema_create(&topology_sema, 0)) != VSTATUS_OK) {
		IB_FATAL_ERROR("can't initialize topology semaphore");
	}

	if ((status = cs_sema_create(&topology_rcv_sema, 0)) != VSTATUS_OK) {
		IB_FATAL_ERROR("can't initialize topology receive semaphore");
	}

	if ((status = cs_sema_create(&sa_sema, 0)) != VSTATUS_OK) {
		IB_FATAL_ERROR("can't initialize sa semaphore");
	}

    //
    //	Initialize the locks
    //
	status = vs_lock_init(&old_topology_lock, VLOCK_FREE, VLOCK_RWTHREAD);
	if (status != VSTATUS_OK) {
		IB_FATAL_ERROR("can't initialize old_topology lock");
	}

	status = vs_lock_init(&new_topology_lock, VLOCK_FREE, VLOCK_THREAD);
	if (status != VSTATUS_OK) {
		IB_FATAL_ERROR("can't initialize new_topology lock");
	}

	status = vs_lock_init(&tid_lock, VLOCK_FREE, VLOCK_THREAD);
	if (status != VSTATUS_OK) {
		IB_FATAL_ERROR("can't initialize tid lock");
	}

	status = vs_lock_init(&sa_lock, VLOCK_FREE, VLOCK_THREAD);
	if (status != VSTATUS_OK) {
		IB_FATAL_ERROR("can't initialize sa lock");
	}

	status = vs_lock_init(&handover_sent_lock, VLOCK_FREE, VLOCK_THREAD);
	if (status != VSTATUS_OK) {
		IB_FATAL_ERROR("can't initialize handover_sent lock");
	}

	triggered_handover= 0;
	handover_sent = 0;

	status = vs_lock_init(&sm_mcSpanningTreeRootGuidLock, VLOCK_FREE, VLOCK_THREAD);
	if (status != VSTATUS_OK) {
		IB_FATAL_ERROR("can't initialize Multicast Spanning Tree Guid lock");
	}
    //
    //	Initialize the MAI subsystem.
    //
	mai_set_num_end_ports( MIN(2*sm_config.subnet_size, MAI_MAX_QUEUED_DEFAULT));
	mai_init();

	status = ib_init_devport(&sm_config.hca, &sm_config.port, &sm_config.port_guid);
	if (status != VSTATUS_OK)
		IB_FATAL_ERROR("sm_main: Failed to bind to device; terminating");
	sm_portguid = sm_config.port_guid;
	if (ib_register_sm((int)sa_max_cntxt+32) != VSTATUS_OK)
		IB_FATAL_ERROR("sm_main: Failed to register management classes; terminating");
#ifndef __VXWORKS__
	{
		char buf[140];

		sprintf(buf, "SM: Using: HFI %u Port %u PortGuid "FMT_U64,
				(unsigned)sm_config.hca+1, (unsigned)sm_config.port, sm_config.port_guid);
		vs_log_output_message(buf, FALSE);
	}
#endif

    //
    //	Open all of the MAI interfaces.
    //
	// used by the SA for new queries
	if ((status = mai_open(1, sm_config.hca, sm_config.port, &fd_sa)) != VSTATUS_OK) {
		IB_FATAL_ERROR("can't open fd_sa");
	}

	// used by the SA to handle RMPP responses and acks
	if ((status = mai_open(1, sm_config.hca, sm_config.port, &fd_sa_w)) != VSTATUS_OK) {
		IB_FATAL_ERROR("can't open fd_sa_w");
	}

	// used by the notice async context to handle SA reports (notices)
	if ((status = mai_open(1, sm_config.hca, sm_config.port, &fd_saTrap)) != VSTATUS_OK) {
		IB_FATAL_ERROR("can't open fd_saTrap");
	}

	// used by the async thread to catch traps and SMInfo requests
	if ((status = mai_open(0, sm_config.hca, sm_config.port, &fd_async)) != VSTATUS_OK) {
		IB_FATAL_ERROR("can't open fd_async");
	}

	// used by the fsm (via async thread) for SMInfo and PortInfo GETs
	if ((status = mai_open(0, sm_config.hca, sm_config.port, &fd_sminfo)) != VSTATUS_OK) {
		IB_FATAL_ERROR("can't open fd_sminfo");
	}

	// used for config consistency in the dbsync thread
	if ((status = mai_open(0, sm_config.hca, sm_config.port, &fd_dbsync)) != VSTATUS_OK) {
		IB_FATAL_ERROR("can't open fd_dbsync");
	}

	// used by the topology thread for sweep SMPs
	if ((status = mai_open(0, sm_config.hca, sm_config.port, &fd_topology)) != VSTATUS_OK) {
		IB_FATAL_ERROR("can't open fd_topology");
	}

	// used by the topology rcv thread for the async context
	// (async LFT, MFT, and GuidInfo)
	if ((status = mai_open(0, sm_config.hca, sm_config.port, &fd_atopology)) != VSTATUS_OK) {
		IB_FATAL_ERROR("can't open fd_atopology");
	}

	// used to transmit loop packets
	if ((status = mai_open(1, sm_config.hca, sm_config.port, &fd_loopTest)) != VSTATUS_OK) {
		IB_FATAL_ERROR("can't open fd_loopTest");
	}

	IB_LOG_INFO("fd_sa", fd_sa);
	IB_LOG_INFO("fd_sa_w", fd_sa_w);
	IB_LOG_INFO("fd_saTrap", fd_saTrap);
	IB_LOG_INFO("fd_async", fd_async);
	IB_LOG_INFO("fd_sminfo", fd_sminfo);
	IB_LOG_INFO("fd_dbsync", fd_dbsync);
	IB_LOG_INFO("fd_topology", fd_topology);
	IB_LOG_INFO("fd_atopology", fd_atopology);
	IB_LOG_INFO("fd_loopTest", fd_loopTest);

    //
    //	Create the SMInfo_t structure.
    //
	sm_state = sm_prevState = SM_STATE_NOTACTIVE;
	sm_smInfo.PortGUID = sm_portguid;
	sm_smInfo.SM_Key = sm_config.sm_key;
	sm_smInfo.ActCount = 0;
	sm_smInfo.u.s.Priority = sm_config.priority;
	sm_smInfo.u.s.InitialPriority = sm_config.priority;
	sm_smInfo.u.s.ElevatedPriority = sm_config.elevated_priority;
	sm_masterStartTime = 0;

#if defined __VXWORKS__
	smCsmSetLogSmDesc(Ics_GetIBDesc(), 0, sm_portguid);
#else
	gethostname(hostName, 64);
	smCsmSetLogSmDesc(hostName, sm_config.port, sm_portguid);
#endif

    //
    // Initialize the SA data structures
    //
    if (sa_main()) {
		IB_FATAL_ERROR("can't initialize SA data structures");
		return(VSTATUS_BAD);
	}

	//
	// Initialize async thread static data
	//
	(void)async_init();

    /*
     * Init SM dbsync thread data
    */
    (void)sm_dbsync_init();

	// initialize job management
	status = sm_jm_init_job_table();
	if (status != VSTATUS_OK) {
		IB_FATAL_ERROR("Failed to initialize Job Management data structures");
		return VSTATUS_BAD;
	}
   
    // Initialize the SSL/TLS network security interface
    if (sm_config.SslSecurityEnabled) 
        (void)if3_ssl_init(&sm_pool);

//
//	Start the SA reader thread.
//
#ifdef __VXWORKS__
	sm_threads[SM_THREAD_SA_READER].id = (uint8_t*)"esm_sar";
#else
	sm_threads[SM_THREAD_SA_READER].id = (void *)"sareader";
#endif
	sm_threads[SM_THREAD_SA_READER].function = sa_main_reader;

	status = sm_start_thread(&sm_threads[SM_THREAD_SA_READER]);
	if (status != VSTATUS_OK) {
		IB_FATAL_ERROR("can't create SA reader thread");
		return(VSTATUS_BAD);
	}

    //
    //	Start the SA writer thread.
    //
#ifdef __VXWORKS__
	sm_threads[SM_THREAD_SA_WRITER].id = (uint8_t*)"esm_saw";
#else
	sm_threads[SM_THREAD_SA_WRITER].id = (void *)"sawriter";
#endif
	sm_threads[SM_THREAD_SA_WRITER].function = sa_main_writer;

	status = sm_start_thread(&sm_threads[SM_THREAD_SA_WRITER]);
	if (status != VSTATUS_OK) {
		IB_FATAL_ERROR("can't create SA writer thread");
		return(VSTATUS_BAD);
	}

    //
    //	Start the TOPOLOGY thread.
    // 
#ifdef __VXWORKS__
	sm_threads[SM_THREAD_TOPOLOGY].id = (uint8_t*)"esm_top";
#else
	sm_threads[SM_THREAD_TOPOLOGY].id = (void *)"topology";
#endif
	sm_threads[SM_THREAD_TOPOLOGY].function = topology_main;

	status = sm_start_thread(&sm_threads[SM_THREAD_TOPOLOGY]);
	if (status != VSTATUS_OK) {
		IB_FATAL_ERROR("can't create TOPOLOGY thread");
		return(VSTATUS_BAD);
	}

    //
    //	Start the ASYNC thread.
    //
#ifdef __VXWORKS__
	sm_threads[SM_THREAD_ASYNC].id = (uint8_t*)"esm_asy";
#else
	sm_threads[SM_THREAD_ASYNC].id = (void *)"async";
#endif
	sm_threads[SM_THREAD_ASYNC].function = async_main;

	status = sm_start_thread(&sm_threads[SM_THREAD_ASYNC]);
	if (status != VSTATUS_OK) {
		IB_FATAL_ERROR("can't create ASYNC thread");
		return(VSTATUS_BAD);
	}

    //
    //	Start the SM topology discovery async receive thread.
    //
#ifdef __VXWORKS__
	sm_threads[SM_THREAD_TOP_RCV].id = (uint8_t*)"esm_rcv";
#else
	sm_threads[SM_THREAD_TOP_RCV].id = (void *)"topology rcv";
#endif
	sm_threads[SM_THREAD_TOP_RCV].function = topology_rcv;

	status = sm_start_thread(&sm_threads[SM_THREAD_TOP_RCV]);
	if (status != VSTATUS_OK) {
		IB_FATAL_ERROR("can't create SM topology async receive thread");
		return(VSTATUS_BAD);
	}

    //
    //	Start the SM topology discovery async receive thread.
    //
#ifdef __VXWORKS__
	sm_threads[SM_THREAD_DBSYNC].id = (uint8_t*)"esm_dbs";
#else
	sm_threads[SM_THREAD_DBSYNC].id = (void *)"sm_dbsync";
#endif
	sm_threads[SM_THREAD_DBSYNC].function = sm_dbsync;

	status = sm_start_thread(&sm_threads[SM_THREAD_DBSYNC]);
	if (status != VSTATUS_OK) {
		IB_FATAL_ERROR("can't create SM db sync thread");
		return(VSTATUS_BAD);
	}

#ifdef __VXWORKS__
    // just exit
#else

    //	Start the PM thread.
    if (startPM) {
        pm_running = 1;
    	sm_threads[SM_THREAD_PM].id = (void *)"pm";
    	sm_threads[SM_THREAD_PM].function = unified_sm_pm;
    
    	status = sm_start_thread(&sm_threads[SM_THREAD_PM]);
    	if (status != VSTATUS_OK) {
    		IB_FATAL_ERROR("can't create Performance Manager thread");
    		return(VSTATUS_BAD);
    	}
    }

#ifdef FE_THREAD_SUPPORT_ENABLED
    //	Start the FE thread.
    if (startFE) {
        fe_running = 1;
    	sm_threads[SM_THREAD_FE].id = (void *)"fe";
    	sm_threads[SM_THREAD_FE].function = unified_sm_fe;
    
    	status = sm_start_thread(&sm_threads[SM_THREAD_FE]);
    	if (status != VSTATUS_OK) {
    		IB_FATAL_ERROR("can't create Fabric Executive thread");
    		return(VSTATUS_BAD);
    	}
    }
#endif

	sm_conf_server_init();

#ifndef __VXWORKS__
	if (xml_trace)
		fprintf(stdout, "\nSM Initial Config Done\n");
#endif

    //
    //	Just loop forever.
    //

	while (sm_control_cmd != SM_CONTROL_SHUTDOWN) {
		vs_thread_sleep(VTIMER_1S);

		if (sm_control_cmd == SM_CONTROL_RECONFIG) {

			// Handle reconfiguration request.
			// If one is already in progress,
			// let it complete before starting
			// a new one.
			smProcessReconfigureRequest();
			//reset the sm_control_cmd
			sm_control_cmd = 0;
		}
	}

#ifdef CAL_IBACCESS
    mai_umadt_read_kill();
    /* give readers time to exit else kernel panic */
	vs_thread_sleep(VTIMER_1S/2);
    cal_ibaccess_global_shutdown();
#endif
    /* Wait 1 second for our threads to die. */
	vs_thread_sleep(VTIMER_1S);

	/* Release VF config only after the threads are stopped as the
	 * threads might be using VF related data.
	 */
	VirtualFabrics_t *VirtualFabricsToRelease = old_topology.vfs_ptr;
	releaseVirtualFabricsConfig(VirtualFabricsToRelease);

#endif

	IB_EXIT(__func__, VSTATUS_OK);
	return(VSTATUS_OK);
}

Status_t sm_clearIsSM(void) {
#if defined(IB_STACK_OPENIB)
	return ib_disable_is_sm();
#elif defined(CAL_IBACCESS)
	Status_t	status;
	uint32_t	mask;
	
	/* we set/clear the isSm capability mask when we register/unregister with the Ism SM class */
	status = sm_get_CapabilityMask(fd_topology, sm_config.port, &mask);
	if (status != VSTATUS_OK) {
		IB_LOG_ERRORRC("can't get isSM rc:", status);
		return status;
	}
	
	mask &= ~PI_CM_IS_SM;
	status = sm_set_CapabilityMask(fd_topology, sm_config.port, mask);
	if (status != VSTATUS_OK) {
		IB_LOG_ERRORRC("can't clear isSM rc:", status);
	}

	return status;

#else
	return VSTATUS_NOSUPPORT;
#endif
}

void
sm_setPriority(uint32_t priority){
	/* Update the global and the value in the global sm_info structure */
	sm_smInfo.u.s.Priority = priority;
	sm_config.priority = priority;
}

uint32_t sm_get_smPerfDebug(void)
{
	return smDebugPerf;
}
 
void smPerfDebugOn(void)
{
    smDebugPerf = 1;
}

void smPerfDebugOff(void)
{
    smDebugPerf = 0;
}

void smPerfDebugToggle(void)
{
	if (smDebugPerf) {
		smDebugPerf = 0;
	} else {
		smDebugPerf = 1;
	}
}

void smForceRebalanceToggle(void)
{
	if (sm_config.force_rebalance) {
		IB_LOG_INFINI_INFO0("Turning OFF ForceRebalance");
		sm_config.force_rebalance = 0;
	} else {
		IB_LOG_INFINI_INFO0("Turning ON ForceRebalance");
		IB_LOG_INFINI_INFO0("will rebalance static routes on next sweep");
		sm_config.force_rebalance = 1;
		forceRebalanceNextSweep = 1;
	}
}

uint32_t sm_get_smAdaptiveRoutingConfigured(void) {
	return sm_config.adaptiveRouting.enable;
}

uint32_t sm_get_smAdaptiveRouting(void) {
	return sm_adaptiveRouting.enable;
}

void smAdaptiveRoutingToggle(uint32_t externalCmd)
{
	if (sm_adaptiveRouting.enable) {
		IB_LOG_INFINI_INFO0("Disabling Adaptive Routing");
		memset(&sm_adaptiveRouting, 0, sizeof(SmAdaptiveRouting_t));
		if (externalCmd) {
			sm_forceSweep("Disabling Adaptive Routing");
		}

	} else if (sm_config.adaptiveRouting.enable) {
		if (!esmLoopTestOn) {
			IB_LOG_INFINI_INFO0("Re-enabling Adaptive Routing");
			sm_adaptiveRouting.enable = sm_config.adaptiveRouting.enable;
			sm_adaptiveRouting.algorithm = sm_config.adaptiveRouting.algorithm;
			sm_adaptiveRouting.debug = sm_config.adaptiveRouting.debug;
			sm_adaptiveRouting.lostRouteOnly = sm_config.adaptiveRouting.lostRouteOnly;
			sm_adaptiveRouting.arFrequency = sm_config.adaptiveRouting.arFrequency;

			// Requires a forced rebalance to setup tables.
			forceRebalanceNextSweep = 1;
			if (externalCmd) {
				sm_forceSweep("Re-enabling Adaptive Routing");
			}
		} else {
			IB_LOG_WARN0("Cannot re-enable Adaptive Routing while Loop Test is running!");
		}

	} else if (externalCmd) {
		if (!esmLoopTestOn) {
			memset(&sm_adaptiveRouting, 0, sizeof(SmAdaptiveRouting_t));
			sm_adaptiveRouting.enable = 1;

			// Requires a forced rebalance to setup tables.
			forceRebalanceNextSweep = 1;
			sm_forceSweep("Enabling Adaptive Routing");
		} else {
			IB_LOG_WARN0("Cannot re-enable Adaptive Routing while Loop Test is running!");
		}

	}
}

void smSetAdaptiveRouting(uint32_t set) {
	if (set != sm_adaptiveRouting.enable) {
		smAdaptiveRoutingToggle(1);
	}
}

void smPauseResumeSweeps(boolean pauseSweeps) {

	sweepsPaused = pauseSweeps;

	if (sweepsPaused)
		IB_LOG_INFINI_INFO0("SM Sweeps Paused; Sweeps will not continue till a resumeSweeps command is received");
	else {
		IB_LOG_INFINI_INFO0("SM Sweeps Resuming");
		sm_forceSweep("SM Sweeps Resumed");
	}
}

boolean
vfChangesDetected(FMXmlCompositeConfig_t *xml_config, VirtualFabrics_t *newVirtualFabrics, VirtualFabrics_t *oldVirtualFabrics){

	boolean vfsChanged = FALSE;

	if ( (oldVirtualFabrics) && (newVirtualFabrics) ) {

		//check number of vfs; if it changed activeStandbyVfsChanged=TRUE
		if (oldVirtualFabrics->number_of_vfs != newVirtualFabrics->number_of_vfs) {
			vfsChanged = TRUE;
		}
		else {
			//loop on all VFs and check for differences
			int vf;
			VF_t* newVfp;
			VF_t* oldVfp;
			
			for (vf=0; vf < oldVirtualFabrics->number_of_vfs; vf++) {
				oldVfp = &oldVirtualFabrics->v_fabric[vf];
				newVfp = &newVirtualFabrics->v_fabric[vf];

				//Optimize: Can probably optimize this; if activeStandby changed, just break out of loop?
				//If changing active/standby list is going to have to do the same logic as what
				//we do when BW is updated, then just break the loop and go to work

				if ( (!strncmp(&oldVfp->name[0], &newVfp->name[0], MAX_VFABRIC_NAME+1) == 0) || 
					 (oldVfp->percent_bandwidth != newVfp->percent_bandwidth) ) {

					//if names are different, that means there were active/standby VF changes
					vfsChanged = TRUE;
					break;
				}
			}
		}
	}
	return vfsChanged;

}

boolean
preProcessVfChanges(VirtualFabrics_t *newVirtualFabrics) {

	boolean processingError = FALSE;

	sm_init_vf_info();

	sm_process_vf_qos_params(newVirtualFabrics);

	processingError = sm_error_check_vfs(newVirtualFabrics);

	return processingError;
}

void
smApplyLogLevelChange(FMXmlCompositeConfig_t *xml_config){
	uint32_t currentLogLevel=sm_config.log_level;
	uint32_t newLogLevel=xml_config->fm_instance[sm_instance]->sm_config.log_level;

#ifndef __VXWORKS__
	PMXmlConfig_t* new_pm_config=&xml_config->fm_instance[sm_instance]->pm_config;
#endif	
	if (currentLogLevel != newLogLevel) {
		sm_set_log_level(newLogLevel);
		sm_config.log_level = newLogLevel;

	}

#ifndef __VXWORKS__
	pmApplyLogLevelChange(new_pm_config);
#endif

}

void
smApplyVfChanges(FMXmlCompositeConfig_t *xml_config, VirtualFabrics_t *newVirtualFabrics){

	sm_assign_base_sls(newVirtualFabrics);
	sm_update_bw(newVirtualFabrics);
	sm_assign_scs_to_sls_FixedMap(newVirtualFabrics);
	sm_resolve_pkeys_for_vfs(newVirtualFabrics);
}

void
smLogLevelOverride(void){
	sm_set_log_level(sm_log_level);
}

void
smProcessReconfigureRequest(void){

	FMXmlCompositeConfig_t *new_xml_config;
	VirtualFabrics_t *oldVirtualFabrics = NULL;
	VirtualFabrics_t *newVirtualFabrics;
#ifdef __VXWORKS__
    uint32_t embedded = 1;
#else
    uint32_t embedded = 0;
#endif

	IB_ENTER(__func__, 0, 0, 0, 0);

	//Optimization:  Should not allocate memory and release memory every time we receive
	//a reconfigure request.  We should be able to do this once on startup and cleanup
	//to reduce the time a reconfigure request takes to perform.
	//uint32_t xml_bytes_needed;
	//xml_bytes_needed = xml_compute_pool_size(/* one instance of sm */ 0);
	//new_xml_config = (FMXmlCompositeConfig_t*)malloc(xml_bytes_needed);

    IB_LOG_INFINI_INFO0("SM: Processing reconfiguration request");

	// Kind of convoluted. If there is an updatedVirtualFabrics pointer, that is the
	// "previous" configuration. If not, then the previous configuration is in
	// the old_topology.
	(void)vs_lock(&new_topology_lock);
	if (updatedVirtualFabrics) oldVirtualFabrics = updatedVirtualFabrics;
	(void)vs_unlock(&new_topology_lock);

	new_xml_config = parseFmConfig(sm_config_filename, IXML_PARSER_FLAG_NONE, sm_instance, /* full parse */ 0, /* embedded */ embedded);
	if (!new_xml_config || !new_xml_config->fm_instance[sm_instance]) {
		IB_LOG_WARN0("SM: Error processing reconfigure request; parseFmConfig failed on new XML file");
	}
	else {
		//If overall checksum has not changed, no dynamic configuration updates to do
		boolean configChanged = FALSE;

		newVirtualFabrics = renderVirtualFabricsConfig(sm_instance, new_xml_config, &new_xml_config->fm_instance[sm_instance]->sm_config, NULL);

		if (newVirtualFabrics != NULL) {

			(void)vs_rdlock(&old_topology_lock);
			if (!oldVirtualFabrics) oldVirtualFabrics = old_topology.vfs_ptr;

			if ( (sm_config.overall_checksum != new_xml_config->fm_instance[sm_instance]->sm_config.overall_checksum) ||
				 (pm_config.overall_checksum != new_xml_config->fm_instance[sm_instance]->pm_config.overall_checksum) ||
				 (oldVirtualFabrics->overall_checksum != newVirtualFabrics->overall_checksum) ) {
				configChanged = TRUE;
			}

			if (configChanged) {

				//Verify that no disruptive checksums have changed

				if (!sm_config_valid(new_xml_config, newVirtualFabrics, oldVirtualFabrics) || !pm_config_valid(new_xml_config)) {
					IB_LOG_WARN0("SM: Failed processing reconfigure request; XML contains invalid changes; reconfiguration request being ignored");

					(void)vs_rwunlock(&old_topology_lock);
					releaseVirtualFabricsConfig(newVirtualFabrics);
				}
				else {

					boolean vfChangesInvalid = FALSE;

					if (vfChangesDetected(new_xml_config, newVirtualFabrics, oldVirtualFabrics)) {
#ifdef USE_FIXED_SCVL_MAPS
						vfChangesInvalid = preProcessVfChanges(newVirtualFabrics);
#else
						//warn user that VF changes can't be applied if they aren't using fixed SCVL maps
						IB_LOG_WARN0("SM: VF changes can't be applied when not using fixed SCVL maps");

						vfChangesInvalid = TRUE;
#endif 
					}
					(void)vs_rwunlock(&old_topology_lock);


					if (vfChangesInvalid) {

						IB_LOG_WARN0("SM: Error processing reconfigure request; VF changes determined to be invalid; reconfiguration request being ignored");

						//reset vf info (not sure if anyone else would be using this stuff after a reconfiguration request is ignored, but reset to be save)
						sm_init_vf_info();

						releaseVirtualFabricsConfig(newVirtualFabrics);
					}
					else {
						smApplyLogLevelChange(new_xml_config);

						// Lock out any sweeps
						(void)vs_lock(&new_topology_lock);

						// If we have an updated Virtual Fabric, that means
						// no sweep has occurred since the last reconfigure
						// (possibly because we are a standby). Ditch the
						// previous reconfigure update, and switch to the
						// current value.
						if (updatedVirtualFabrics) {
							releaseVirtualFabricsConfig(updatedVirtualFabrics);
						}
						updatedVirtualFabrics = newVirtualFabrics;

						smApplyVfChanges(new_xml_config, updatedVirtualFabrics);

						// Allow sweeps to run
						(void)vs_unlock(&new_topology_lock);

						/* Update our consistency checksums */
    					sm_dbsync_checksums(updatedVirtualFabrics->consistency_checksum,
                       						new_xml_config->fm_instance[sm_instance]->sm_config.consistency_checksum,
                       						new_xml_config->fm_instance[sm_instance]->pm_config.consistency_checksum, 
                       						new_xml_config->fm_instance[sm_instance]->fe_config.consistency_checksum);
						sm_config.overall_checksum = new_xml_config->fm_instance[sm_instance]->sm_config.overall_checksum;
			 			pm_config.overall_checksum = new_xml_config->fm_instance[sm_instance]->pm_config.overall_checksum;

						if (sm_state == SM_STATE_MASTER) {
    						SmRecKeyp       smreckeyp;
    						SmRecp          smrecp;
    						CS_HashTableItr_t itr;
							Status_t status;

							/*
							 * Notify all standby SMs to reread their configuration.
							 */
    						/* lock out service record hash table */
    						if ((status = vs_lock(&smRecords.smLock)) != VSTATUS_OK) {
        						IB_LOG_ERRORRC("Can't lock SM Record table, rc:", status);
    						} else {
								if (cs_hashtable_count(smRecords.smMap) > 1) {
           							cs_hashtable_iterator(smRecords.smMap, &itr);
           							do {
                						smrecp = cs_hashtable_iterator_value(&itr);
                						smreckeyp = cs_hashtable_iterator_key(&itr);
                						if (smrecp->portguid == sm_smInfo.PortGUID) {
											/* Skip us */
											continue;
                						} else if (smrecp->smInfoRec.SMInfo.u.s.SMStateCurrent <= SM_STATE_STANDBY) {
    										IB_LOG_INFINI_INFO_FMT(__func__,
                                            	"SM: Forwarding reconfiguration request to standby SM at Lid 0x%x, portGuid "FMT_U64,
                                   				smrecp->lid, *smreckeyp);
                            				(void) sm_dbsync_queueMsg(DBSYNC_TYPE_RECONFIG, DBSYNC_DATATYPE_NONE, smrecp->lid, smrecp->portguid, smrecp->isEmbedded, NULL);
                						}
        							} while (cs_hashtable_iterator_advance(&itr));
								}
    							vs_unlock(&smRecords.smLock);
							}
							// After a reconfiguration, force a resweep
							sm_trigger_sweep(SM_SWEEP_REASON_RECONFIG);
						}
    					IB_LOG_INFINI_INFO0("SM: Reconfiguration completed successfully");
					}
				}
			}
			else {
				(void)vs_rwunlock(&old_topology_lock);

    			IB_LOG_INFINI_INFO0("SM: No configuration changes to process; reconfiguration request being ignored");

				//if nothing changed, release the new VirtualFabrics*
				releaseVirtualFabricsConfig(newVirtualFabrics);
			}

		} //if newVirtualFabrics = NULL

	}

	//Optimize: release temp xml memory on sm cleanup, not every time we process
	//reconfigure request

	//Release the temp xml_config memory
	releaseXmlConfig(new_xml_config, /* full */ 1);
	new_xml_config = NULL;

	IB_EXIT(__func__, VSTATUS_OK);
}


void
sm_shutdown(void){
	(void)pm_main_kill();
    (void)fe_main_kill();
	sa_main_kill();
	topology_main_kill();
	async_main_kill();
    topology_rcv_kill();
    sm_dbsync_kill();
	sm_jm_destroy_job_table();
	sm_clean_vfdg_memory();
	sm_free_vf_mem();

#ifdef __VXWORKS__
	/* Release VF config only after the threads are stopped as the
	 * threads might be using VF related data.
	 */
	VirtualFabrics_t *VirtualFabricsToRelease = old_topology.vfs_ptr;
	releaseVirtualFabricsConfig(VirtualFabricsToRelease);
#endif
}

void
sm_clean_vfdg_memory(void){

#ifndef __VXWORKS__
	// clear memory used for DG and VF config information
	int idx;
	for (idx = 0; idx<vf_config.number_of_vfs; idx++) {
		vs_pool_free(&sm_pool, (void*)vf_config.vf[idx]);
	}

	for (idx = 0; idx<dg_config.number_of_dgs; idx++) {
		vs_pool_free(&sm_pool, (void*)dg_config.dg[idx]);
	}

#else
	//VxWorks cleans this memory in Esm_Init.c
#endif

}

#ifdef __VXWORKS__
void
sm_cleanGlobals(uint8_t stop){

	if (stop) {
		sm_spanning_tree_resetGlobals();

		if (sm_pool.name[0] != 0) vs_pool_delete (&sm_pool);

    	memset(&sm_pool,0,sizeof(sm_pool));

		lidmap = NULL;

    	memset(sm_env,0,sizeof(sm_env));
	}

    sm_state = sm_prevState = SM_STATE_NOTACTIVE;
    sm_portguid = 0;
    sm_control_cmd = 0;
	numMcGroupClasses = 0;

	sm_useIdealMcSpanningTreeRoot = 1;
	sm_mcSpanningTreeRoot_useLeastWorstCaseCost = 0;
	sm_mcSpanningTreeRoot_useLeastTotalCost = 1;

	sm_mcSpanningTreeRootGuid = 0;
	sm_mcRootCostDeltaThreshold = DEFAULT_MCROOT_COST_IMPROVEMENT_PERCENTAGE;

	sm_lmc_0_freeLid_hint = 0;
	sm_lmc_e0_freeLid_hint = 0;
	sm_lmc_freeLid_hint = 0;

	if (stop) {
		sm_lid_lo = 1;
		sm_lid_hi = UNICAST_LID_MAX;

		memset(&sm_smInfo,0,sizeof(sm_smInfo));
		sm_masterStartTime = 0;
		sm_McGroups = 0;
    	sm_numMcGroups = 0;
    	sm_McGroups_Need_Prog = 0;

    	memset(&fd_sa,0,sizeof(fd_sa));
		memset(&fd_saTrap,0,sizeof(fd_saTrap));
    	memset(&fd_async,0,sizeof(fd_async));
    	memset(&fd_sminfo,0,sizeof(fd_sminfo));
    	memset(&fd_dbsync,0,sizeof(fd_dbsync));
    	memset(&fd_topology,0,sizeof(fd_topology));
    	memset(&fd_atopology,0,sizeof(fd_atopology));
    	memset(&fd_loopTest,0,sizeof(fd_loopTest));

		sm_threads = NULL;

		esmLoopTestOn = 0;
		esmLoopTestAllowAdaptiveRouting = 0;
		esmLoopTestFast = 0;
		esmLoopTestInjectNode = -1;
		esmLoopTestNumPkts = 1;
		esmLoopTestPathLen = DEFAULT_LOOP_PATH_LENGTH;
		esmLoopTestMinISLRedundancy = 4;
		esmLoopTestTotalPktsInjected = 0;
		esmLoopTestInjectEachSweep = DEFAULT_LOOP_INJECT_EACH_SWEEP;
		esmLoopTestForceInject = 0;
	}

}

void smShowPacketCount()
{
	printf("SM's packet count (sm_smInfo.ActCount) is %d\n", (int)sm_smInfo.ActCount);
}

#endif
