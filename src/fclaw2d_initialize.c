/*
Copyright (c) 2012 Carsten Burstedde, Donna Calhoun
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

 * Redistributions of source code must retain the above copyright notice, this
list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation
and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <fclaw2d_global.h>

#include <fclaw2d_forestclaw.h>
#include <fclaw2d_partition.h>
#include <fclaw2d_exchange.h>
#include <fclaw2d_physical_bc.h>
#include <fclaw2d_regrid.h>
#include <fclaw2d_ghost_fill.h>

#if defined(_OPENMP)
#include <omp.h>
#endif

/* -----------------------------------------------------------------
   Initial grid
   ----------------------------------------------------------------- */
static
void cb_initialize (fclaw2d_domain_t *domain,
                    fclaw2d_patch_t *this_patch,
                    int this_block_idx,
                    int this_patch_idx,
                    void *user)
{
    fclaw2d_global_iterate_t* g = (fclaw2d_global_iterate_t*) user;

    fclaw2d_build_mode_t build_mode = FCLAW2D_BUILD_FOR_UPDATE;

    fclaw2d_patch_data_new(g->glob,this_patch);
    fclaw2d_patch_build(g->glob,this_patch,
                        this_block_idx,
                        this_patch_idx,
                        &build_mode);
    fclaw2d_patch_initialize(g->glob,this_patch,this_block_idx,this_patch_idx);
}



/* -----------------------------------------------------------------
   Public interface
   ----------------------------------------------------------------- */
void fclaw2d_initialize(fclaw2d_global_t *glob)
{
    fclaw2d_domain_t** domain = &glob->domain;

    int time_interp = 0;
    char basename[BUFSIZ];
    const amr_options_t *gparms = glob->gparms;

    fclaw2d_domain_data_t* ddata = fclaw2d_domain_get_data(*domain);

    /* This mapping context is needed by fortran mapping functions */
    fclaw2d_map_context_t *cont = fclaw2d_domain_get_map_context(*domain);
    SET_CONTEXT(&cont);

    int maxthreads = 0;
#if defined(_OPENMP)
    maxthreads = omp_get_max_threads();
#endif
    fclaw_global_essentialf("Max threads set to %d\n",maxthreads);

    int minlevel = gparms->minlevel;
    int maxlevel = gparms->maxlevel;

    /* Initialize all timers */
    ddata->is_latest_domain = 1;
    int i;
    for (i = 0; i < FCLAW2D_TIMER_COUNT; ++i) {
        fclaw2d_timer_init (&ddata->timers[i]);
    }

    /* start timing */
    fclaw2d_domain_barrier (*domain);
    fclaw2d_timer_start (&ddata->timers[FCLAW2D_TIMER_WALLTIME]);
    fclaw2d_timer_start (&ddata->timers[FCLAW2D_TIMER_INIT]);

    /* User defined problem setup */
    fclaw2d_problem_setup(glob);

    /* set specific refinement strategy */
    fclaw2d_domain_set_refinement
        (*domain, gparms->smooth_refine, gparms->smooth_refine_level,
         gparms->coarsen_delay);


    /* ------------------------------------------------
       Set up initial domain.

       This needs to be set as if it were going to be used
       for updating.
       ------------------------------------------------ */

    /* Get an initial domain */
    fclaw2d_domain_setup(glob,*domain);

    /* Initialize patches on uniformly refined level minlevel */
    fclaw2d_timer_start (&ddata->timers[FCLAW2D_TIMER_REGRID_BUILD]);
    fclaw2d_global_iterate_level(glob, minlevel, cb_initialize,
                                 NULL);
    fclaw2d_timer_stop (&ddata->timers[FCLAW2D_TIMER_REGRID_BUILD]);

    /* Set up ghost patches */
    fclaw2d_exchange_setup(glob,FCLAW2D_TIMER_INIT);

    /* This is normally called from regrid */
    fclaw2d_regrid_set_neighbor_types(glob);

    /* We need a user option here to set ghost values after initialization */
    if (gparms->init_ghostcell){
        fclaw2d_ghost_update(glob,(*domain)->global_minlevel,
                             (*domain)->global_maxlevel,0.0,
                             time_interp,FCLAW2D_TIMER_INIT);
    }
    fclaw2d_physical_set_bc(glob,(*domain)->global_minlevel,
                            0.0,time_interp);

    // VTK output during amrinit
    if (gparms->vtkout & 1) {
        // into timer
        fclaw2d_timer_stop (&ddata->timers[FCLAW2D_TIMER_INIT]);
        fclaw2d_timer_start (&ddata->timers[FCLAW2D_TIMER_OUTPUT]);

        // output
        snprintf (basename, BUFSIZ, "%s_init_level_%02d",
                  gparms->prefix, minlevel);
        fclaw2d_output_write_vtk (glob, basename);

        // out of timer
        fclaw2d_timer_stop (&ddata->timers[FCLAW2D_TIMER_OUTPUT]);
        fclaw2d_timer_start (&ddata->timers[FCLAW2D_TIMER_INIT]);
    }
    /* ------------------------------------------------
       Done with initial setup.
       ------------------------------------------------ */


    /* ------------------------------------------------
       Build up an initial refinement.
       ------------------------------------------------ */
    if (minlevel < maxlevel)
    {
        int domain_init = 1;
        int level;
        for (level = minlevel; level < maxlevel; level++)
        {
            fclaw2d_timer_start (&ddata->timers[FCLAW2D_TIMER_REGRID_TAGGING]);
            fclaw2d_global_iterate_level(glob, level,
                                         cb_fclaw2d_regrid_tag4refinement,
                                         &domain_init);
            fclaw2d_timer_stop (&ddata->timers[FCLAW2D_TIMER_REGRID_TAGGING]);

            // Construct new domain based on tagged patches.
            fclaw2d_timer_stop (&ddata->timers[FCLAW2D_TIMER_INIT]);
            fclaw2d_timer_start (&ddata->timers[FCLAW2D_TIMER_ADAPT_COMM]);
            fclaw2d_domain_t *new_domain = fclaw2d_domain_adapt(*domain);

            int have_new_refinement = new_domain != NULL;

            if (have_new_refinement)
            {
                /* Have to get a new ddata */
                fclaw2d_domain_setup(glob,new_domain);
                ddata = fclaw2d_domain_get_data(new_domain);
            }

            fclaw2d_timer_stop (&ddata->timers[FCLAW2D_TIMER_ADAPT_COMM]);
            fclaw2d_timer_start (&ddata->timers[FCLAW2D_TIMER_INIT]);

            if (have_new_refinement)
            {
                fclaw_global_infof(" -- Have new initial refinement\n");

                /* Re-initialize new grids.   Ghost cell values needed for
                   interpolation have already been set by initialization */
                fclaw2d_timer_start (&ddata->timers[FCLAW2D_TIMER_REGRID_BUILD]);
                fclaw2d_global_iterate_adapted(glob, new_domain,
                                               cb_fclaw2d_regrid_repopulate,
                                               (void *) &domain_init);

                fclaw2d_timer_stop (&ddata->timers[FCLAW2D_TIMER_REGRID_BUILD]);

                // free all memory associated with old domain
                fclaw2d_domain_reset(glob);
                *domain = new_domain;
                new_domain = NULL;

                // VTK output during amrinit
                if (gparms->vtkout & 1) {
                    // into timer
                    ddata = fclaw2d_domain_get_data (*domain);
                    fclaw2d_timer_stop (&ddata->timers[FCLAW2D_TIMER_INIT]);
                    fclaw2d_timer_start (&ddata->timers[FCLAW2D_TIMER_OUTPUT]);

                    // output
                    snprintf (basename, BUFSIZ, "%s_init_level_%02d_adapt",
                              gparms->prefix, level);
                    fclaw2d_output_write_vtk (glob, basename);

                    /* out of timer */
                    fclaw2d_timer_stop (&ddata->timers[FCLAW2D_TIMER_OUTPUT]);
                    fclaw2d_timer_start (&ddata->timers[FCLAW2D_TIMER_INIT]);
                    ddata = NULL;
                }

                /* Repartition domain to new processors.   Second arg is the
                   mode for VTK output */
                fclaw2d_partition_domain(glob,level,FCLAW2D_TIMER_INIT);

                /* Need a new timer */
                ddata = fclaw2d_domain_get_data(*domain);

                /* Set up ghost patches.  This probably doesn't need to be done
                   each time we add a new level. */
                fclaw2d_exchange_setup(glob,FCLAW2D_TIMER_INIT);

                /* This is normally called from regrid, once the initial domain
                   has been set up */
                fclaw2d_regrid_set_neighbor_types(glob);

            }
            else
            {
                /* We don't have a new refinement, and so can break out of level loop */
                break;
            }
        }  /* Level loop (minlevel --> maxlevel) */
    }

    if (gparms->init_ghostcell)
    {
        fclaw2d_ghost_update(glob,(*domain)->global_minlevel,
                             (*domain)->global_maxlevel,0.0,
                             time_interp,FCLAW2D_TIMER_INIT);
    }

    fclaw2d_after_regrid(*domain);

    /* Print global minimum and maximum levels */
    fclaw_global_infof("Global minlevel %d maxlevel %d\n",
                (*domain)->global_minlevel, (*domain)->global_maxlevel);

    /* Stop timer */
    ddata = fclaw2d_domain_get_data(*domain);
    fclaw2d_timer_stop (&ddata->timers[FCLAW2D_TIMER_INIT]);
}
