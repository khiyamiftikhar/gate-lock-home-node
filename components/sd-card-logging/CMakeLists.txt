idf_component_register ( SRCS logger.c "internals/sd_mount.c"
                    INCLUDE_DIRS "." 
                    PRIV_INCLUDE_DIRS "internals"
                    PRIV_REQUIRES driver sdmmc fatfs sdmmc log-capture time-service )