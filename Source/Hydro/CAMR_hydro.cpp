#include "CAMR.H"
#include "Godunov.H"
#include "CAMR_hydro.H"
#include "CAMR_utils_K.H"
#include "MOL_umeth.H"

using namespace amrex;

void
CAMR_umdrv (bool do_mol, Box const& bx,
            amrex::Geometry const& geom,
            const int* bclo, const int* bchi,
            Array4<const Real> const& uin_arr,
            Array4<      Real> const& dsdt_arr,
            Array4<const Real> const& q_arr,
            Array4<const Real> const& qaux_arr,
            Array4<const Real> const& src_q,
            const amrex::GpuArray<Real, AMREX_SPACEDIM> dx,
            const Real dt,
            const int ppm_type,
            const int plm_iorder,
            const int use_pslope,
            const int use_flattening,
            const int transverse_reset_density,
            const Real small,
            const Real small_dens,
            const Real small_pres,
            const Real l_difmag,
            const amrex::GpuArray<const Array4<Real>, AMREX_SPACEDIM> flx,
            const amrex::GpuArray<const Array4<const Real>, AMREX_SPACEDIM> a,
            Array4<Real> const& vol)
{
    BL_PROFILE_VAR("CAMR::umdrv()", umdrv);

    // Set Up for Hydro Flux Calculations
    auto const& bxg2 = grow(bx, 2);
    FArrayBox qec[AMREX_SPACEDIM];
    for (int dir = 0; dir < AMREX_SPACEDIM; dir++) {
      const Box eboxes = amrex::surroundingNodes(bxg2, dir);
      qec[dir].resize(eboxes, NGDNV, amrex::The_Async_Arena());
    }
    GpuArray<Array4<Real>, AMREX_SPACEDIM> qec_arr{
      {AMREX_D_DECL(qec[0].array(), qec[1].array(), qec[2].array())}};

    const int* domlo = geom.Domain().loVect();
    const int* domhi = geom.Domain().hiVect();

    // Temporary FArrayBoxes
    FArrayBox  divu(bxg2, 1, amrex::The_Async_Arena());
    FArrayBox pdivu(bx  , 1, amrex::The_Async_Arena());
    auto const& divuarr = divu.array();
    auto const& pdivuarr = pdivu.array();

    if (do_mol) {
        MOL_umeth(bx, bclo, bchi, domlo, domhi, q_arr, qaux_arr,
                  AMREX_D_DECL(flx[0], flx[1], flx[2]),
                  AMREX_D_DECL(qec_arr[0], qec_arr[1], qec_arr[2]),
                  AMREX_D_DECL(a[0], a[1], a[2]), pdivuarr, vol,
                  small, small_dens, small_pres, plm_iorder);

    } else {
        Godunov_umeth(bx, bclo, bchi, domlo, domhi, q_arr, qaux_arr, src_q,
                      AMREX_D_DECL(flx[0], flx[1], flx[2]),
                      AMREX_D_DECL(qec_arr[0], qec_arr[1], qec_arr[2]),
                      AMREX_D_DECL(a[0], a[1], a[2]),
                      pdivuarr, vol, dx, dt,
                      small, small_dens, small_pres, ppm_type, use_pslope, use_flattening,
                      plm_iorder, transverse_reset_density);
    }

    // Construct divu
    AMREX_D_TERM(const Real dx0 = dx[0];,
                 const Real dx1 = dx[1];,
                 const Real dx2 = dx[2];);
    GpuArray<int,AMREX_SPACEDIM> ldomlo{AMREX_D_DECL(domlo[0],domlo[1],domlo[2])};
    GpuArray<int,AMREX_SPACEDIM> ldomhi{AMREX_D_DECL(domhi[0],domhi[1],domhi[2])};
    GpuArray<int,AMREX_SPACEDIM> lbclo{AMREX_D_DECL(bclo[0],bclo[1],bclo[2])};
    GpuArray<int,AMREX_SPACEDIM> lbchi{AMREX_D_DECL(bchi[0],bchi[1],bchi[2])};
    ParallelFor(bxg2, [=] AMREX_GPU_DEVICE(int i, int j, int k) noexcept {
        CAMR_divu(i, j, k, q_arr, AMREX_D_DECL(dx0, dx1, dx2), divuarr, ldomlo, ldomhi, lbclo, lbchi);
    });

    // Adjust the fluxes with artificial viscosity and area-weight them
    adjust_fluxes(bx, uin_arr, flx, a, divuarr, dx, domlo, domhi, bclo, bchi, l_difmag);

    CAMR_consup  (bx, dsdt_arr, flx, vol, pdivuarr);

    BL_PROFILE_VAR_STOP(umdrv);
}
