/* -*- c++ -*-
 * Copyright (c) 2012-2014 by the GalSim developers team on GitHub
 * https://github.com/GalSim-developers
 *
 * This file is part of GalSim: The modular galaxy image simulation toolkit.
 * https://github.com/GalSim-developers/GalSim
 *
 * GalSim is free software: redistribution and use in source and binary forms,
 * with or without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions, and the disclaimer given in the accompanying LICENSE
 *    file.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the disclaimer given in the documentation
 *    and/or other materials provided with the distribution.
 */

#define DEBUGLOGGING

#include "SBLinearOpticalet.h"
#include "SBLinearOpticaletImpl.h"
#include "math/Bessel.h"
#include "math/Gamma.h"
#include "Solve.h"

// Define this variable to find azimuth (and sometimes radius within a unit disc) of 2d photons by
// drawing a uniform deviate for theta, instead of drawing 2 deviates for a point on the unit
// circle and rejecting corner photons.
// The relative speed of the two methods was tested as part of issue #163, and the results
// are collated in devutils/external/time_photon_shooting.
// The conclusion was that using sin/cos was faster for icpc, but not g++ or clang++.
#ifdef _INTEL_COMPILER
#define USE_COS_SIN
#endif

namespace galsim {

    SBLinearOpticalet::SBLinearOpticalet(double r0, int n1, int m1, int n2, int m2,
                                         const GSParams& gsparams) :
        SBProfile(new SBLinearOpticaletImpl(r0, n1, m1, n2, m2, gsparams)) {}

    SBLinearOpticalet::SBLinearOpticalet(const SBLinearOpticalet& rhs) : SBProfile(rhs) {}

    SBLinearOpticalet::~SBLinearOpticalet() {}

    double SBLinearOpticalet::getScaleRadius() const
    {
        assert(dynamic_cast<const SBLinearOpticaletImpl*>(_pimpl.get()));
        return static_cast<const SBLinearOpticaletImpl&>(*_pimpl).getScaleRadius();
    }

    int SBLinearOpticalet::getN1() const
    {
        assert(dynamic_cast<const SBLinearOpticaletImpl*>(_pimpl.get()));
        return static_cast<const SBLinearOpticaletImpl&>(*_pimpl).getN1();
    }

    int SBLinearOpticalet::getM1() const
    {
        assert(dynamic_cast<const SBLinearOpticaletImpl*>(_pimpl.get()));
        return static_cast<const SBLinearOpticaletImpl&>(*_pimpl).getM1();
    }

    int SBLinearOpticalet::getN2() const
    {
        assert(dynamic_cast<const SBLinearOpticaletImpl*>(_pimpl.get()));
        return static_cast<const SBLinearOpticaletImpl&>(*_pimpl).getN2();
    }

    int SBLinearOpticalet::getM2() const
    {
        assert(dynamic_cast<const SBLinearOpticaletImpl*>(_pimpl.get()));
        return static_cast<const SBLinearOpticaletImpl&>(*_pimpl).getM2();
    }

    std::string SBLinearOpticalet::SBLinearOpticaletImpl::serialize() const
    {
        std::ostringstream oss(" ");
        oss.precision(std::numeric_limits<double>::digits10 + 4);
        oss << "galsim._galsim.SBLinearOpticalet("<<getScaleRadius();
        oss << ", " << getN1() << ", " << getM1();
        oss << ", " << getN2() << ", " << getM2();
        oss << ", galsim.GSParams("<<gsparams<<"))";
        return oss.str();
    }

    LRUCache<Tuple<int,int,int,int,GSParamsPtr>,LinearOpticaletInfo>
        SBLinearOpticalet::SBLinearOpticaletImpl::cache(sbp::max_linearopticalet_cache);

    SBLinearOpticalet::SBLinearOpticaletImpl::SBLinearOpticaletImpl(double r0,
                                                                    int n1, int m1,
                                                                    int n2, int m2,
                                                                    const GSParams& gsparams) :
        SBProfileImpl(gsparams), _r0(r0)
        //_n1(n1), _m1(m1), _n2(n2), _m2(m2),
        //_info(cache.get(MakeTuple(_n1, _m1, _n2, _m2, this->gsparams.duplicate())))
    {
        if ((n1 < 0) or (n2 < 0) or (m1 < -n1) or (m1 > n1) or (m2 < -n2) or (m2 > n2))
            throw SBError("Requested LinearOpticalet indices out of range");
        if (((n1+m1) & 1) or ((n2+m2) & 1))
            throw SBError("Invalid LinearOpticalet m, n combination");
        if (n1 > n2) { // Keep order: n1 <= n2;
            _n1 = n2;
            _m1 = m2;
            _n2 = n1;
            _m2 = m1;
        } else if (n1 < n2) {
            _n1 = n1;
            _m1 = m1;
            _n2 = n2;
            _m2 = m2;
        } else { // n1 == n2
            _n1 = n1;
            _n2 = n2;
            if (m1 > m2) {
                _m1 = m2;
                _m2 = m1;
            } else {
                _m1 = m1;
                _m2 = m2;
            }
        }

        _info = cache.get(MakeTuple(_n1, _m1, _n2, _m2, GSParamsPtr(this->gsparams)));

        dbg<<"Start LinearOpticalet constructor:\n";
        dbg<<"r0 = "<<_r0<<std::endl;
        dbg<<"(n1,m1) = ("<<_n1<<","<<_m1<<")"<<std::endl;
        dbg<<"(n2,m2) = ("<<_n2<<","<<_m2<<")"<<std::endl;

        _r0_sq = _r0 * _r0;
        _inv_r0 = 1. / _r0;
        _inv_r0_sq = _inv_r0 * _inv_r0;
        _xnorm = M_PI / _r0_sq;
    }

    double SBLinearOpticalet::SBLinearOpticaletImpl::maxK() const
    { return _info->maxK() * _inv_r0; }
    double SBLinearOpticalet::SBLinearOpticaletImpl::stepK() const
    { return _info->stepK() * _inv_r0; }

    double SBLinearOpticalet::SBLinearOpticaletImpl::xValue(const Position<double>& p) const
    {
        double r = sqrt(p.x * p.x + p.y * p.y) * _inv_r0;
        double phi = atan2(p.y, p.x);
        return _xnorm * _info->xValue(r, phi);
    }

    std::complex<double> SBLinearOpticalet::SBLinearOpticaletImpl::kValue(const Position<double>& k) const
    {
        double ksq = (k.x*k.x + k.y*k.y) * _r0_sq;
        double phi = atan2(k.y, k.x);
        return _info->kValue(ksq, phi);
    }

    template <typename T>
    void SBLinearOpticalet::SBLinearOpticaletImpl::fillXImage(ImageView<T> im,
                                                              double x0, double dx, int izero,
                                                              double y0, double dy, int jzero) const
    {
        dbg<<"SBLinearOpticalet fillXValue\n";
        dbg<<"x = "<<x0<<" + i * "<<dx<<", izero = "<<izero<<std::endl;
        dbg<<"y = "<<y0<<" + i * "<<dy<<", jzero = "<<jzero<<std::endl;
        const int m = im.getNCol();
        const int n = im.getNRow();
        std::complex<T>* ptr = im.getData();
        int skip = im.getNSkip();
        assert(im.getStep() == 1);

        x0 *= _inv_r0;
        dx *= _inv_r0;
        y0 *= _inv_r0;
        dy *= _inv_r0;

        for (int j=0; j<n; ++j,y0+=dy,ptr+=skip) {
            double x = x0;
            double ysq = y0*y0;
            for (int i=0;i<m;++i,x+=dx) {
                double rsq = x*x + ysq;
                double phi = atan2(y0, x);
                *ptr++ = _xnorm * _info->xValue(rsq, phi);
            }
        }
    }

    template <typename T>
    void SBLinearOpticalet::SBLinearOpticaletImpl::fillXImage(ImageView<T> im,
                                                              double x0, double dx, double dxy,
                                                              double y0, double dy, double dyx) const
    {
        dbg<<"SBLinearOpticalet fillXValue\n";
        dbg<<"x = "<<x0<<" + i * "<<dx<<" + j * "<<dxy<<std::endl;
        dbg<<"y = "<<y0<<" + i * "<<dyx<<" + j * "<<dy<<std::endl;
        const int m = im.getNCol();
        const int n = im.getNRow();
        std::complex<T>* ptr = im.getData();
        int skip = im.getNSkip();
        assert(im.getStep() == 1);

        x0 *= _inv_r0;
        dx *= _inv_r0;
        dxy *= _inv_r0;
        y0 *= _inv_r0;
        dy *= _inv_r0;
        dyx *= _inv_r0;

        for (int j=0; j<n; ++j,x0+=dxy,y0+=dy,ptr+=skip) {
            double x = x0;
            double y = y0;
            for (int i=0;i<m;++i,x+=dx,y+=dyx) {
                double rsq = x*x + y*y;
                double phi = atan2(y, x);
                *ptr++ = _xnorm * _info->xValue(rsq, phi);
            }
        }
    }

    template <typename T>
    void SBLinearOpticalet::SBLinearOpticaletImpl::fillKImage(ImageView<std::complex<T> > im,
                                                              double kx0, double dkx, int izero,
                                                              double ky0, double dky, int jzero) const
    {
        dbg<<"SBLinearOpticalet fillKValue\n";
        dbg<<"kx = "<<kx0<<" + i * "<<dkx<<", izero = "<<izero<<std::endl;
        dbg<<"ky = "<<ky0<<" + i * "<<dky<<", jzero = "<<jzero<<std::endl;
        const int m = im.getNCol();
        const int n = im.getNRow();
        std::complex<T>* ptr = im.getData();
        int skip = im.getNSkip();
        assert(im.getStep() == 1);

        kx0 *= _r0;
        dkx *= _r0;
        ky0 *= _r0;
        dky *= _r0;

        for (int j=0; j<n; ++j,ky0+=dky,ptr+=skip) {
            double kx = kx0;
            double kysq = ky0*ky0;
            for (int i=0;i<m;++i,kx+=dkx) {
                double ksq = kx*kx + kysq;
                double phi = atan2(ky0, kx);
                *ptr++ = _info->kValue(ksq, phi);
            }
        }
    }

    template <typename T>
    void SBLinearOpticalet::SBLinearOpticaletImpl::fillKImage(ImageView<std::complex<T> > im,
                                                              double kx0, double dkx, double dkxy,
                                                              double ky0, double dky, double dkyx) const
    {
        dbg<<"SBLinearOpticalet fillKValue\n";
        dbg<<"x = "<<kx0<<" + i * "<<dkx<<" + j * "<<dkxy<<std::endl;
        dbg<<"y = "<<ky0<<" + i * "<<dkyx<<" + j * "<<dky<<std::endl;
        const int m = im.getNCol();
        const int n = im.getNRow();
        std::complex<T>* ptr = im.getData();
        int skip = im.getNSkip();
        assert(im.getStep() == 1);

        kx0 *= _r0;
        dkx *= _r0;
        dkxy *= _r0;
        ky0 *= _r0;
        dky *= _r0;
        dkyx *= _r0;

        for (int j=0; j<n; ++j,kx0+=dkxy,ky0+=dky,ptr+=skip) {
            double kx = kx0;
            double ky = ky0;
            for (int i=0;i<m;++i,kx+=dkx,ky+=dkyx) {
                double ksq = kx*kx + ky*ky;
                double phi = atan2(ky, kx);
                *ptr++ = _info->kValue(ksq, phi);
            }
        }
    }

    LinearOpticaletInfo::LinearOpticaletInfo(int n1, int m1, int n2, int m2,
                                             const GSParamsPtr& gsparams) :
        _n1(n1), _m1(m1), _n2(n2), _m2(m2), _gsparams(gsparams),
        _maxk(0.), _stepk(0.),
        _xnorm(0.),
        _ftsum(Table::spline),
        _ftdiff(Table::spline)
    {
        dbg<<"Start LinearOpticaletInfo constructor for (n1,m1,n2,m2) = ("
            <<_n1<<","<<_m1<<","<<_n2<<","<<_m2<<")"<<std::endl;
    }

    // Use code from SBAiry, but ignore obscuration.
    double LinearOpticaletInfo::stepK() const
    {
        // stole this very roughly from SBAiry
        if (_stepk == 0.) {
            double R = 1./ ( _gsparams->folding_threshold * 0.5 * M_PI * M_PI);
            _stepk = M_PI / R;
        }
        return _stepk;
    }

    // Use code from SBAiry, but ignore obscuration.
    double LinearOpticaletInfo::maxK() const
    {
        if (_maxk == 0.) {
            _maxk = 2.*M_PI;
        }
        return _maxk;
    }

    double LinearOpticaletInfo::Vnm(int n, int m, double r) const
    {
        if ((abs(m)+n) & 2)
            return -math::cyl_bessel_j(n+1, r)/r;
        return math::cyl_bessel_j(n+1, r)/r;
    }

    // The workhorse routines...
    double LinearOpticaletInfo::xValue(double r, double phi) const
    {
        if (r == 0.0) {
            if (_n1==0 && _n2 == 0)
                return 0.25;
            return 0.0;
        }
        double ret = Vnm(_n1, _m1, r*M_PI) * Vnm(_n2, _m2, r*M_PI);
        if (_m1 > 0)
            ret *= cos(_m1 * phi);
        else if (_m1 < 0)
            ret *= sin(_m1 * phi);
        if (_m2 > 0)
            ret *= cos(_m2 * phi);
        else if (_m2 < 0)
            ret *= sin(_m2 * phi);
        return ret;
    }

    std::complex<double> LinearOpticaletInfo::kValue(double ksq, double phi) const
        // Need to take the FT of a function with azimuthal product {cos, sin} x {cos, sin}.
        // So use the trig product-to-sum rules to construct two single-azimuthal-mode transforms:
        // cos(a)cos(b) = 0.5*(cos(a+b) + cos(a-b))
        // sin(a)sin(b) = 0.5*(cos(a-b) - cos(a+b))
        // sin(a)cos(b) = 0.5*(sin(a+b) + sin(a-b))
        // cos(a)sin(b) = 0.5*(sin(a+b) - sin(a-b))
        // This means we need to compute 2 Hankel transforms instead of just one: _ftsum and _ftdiff.
    {
        if (_ftsum.size() == 0) setupFT();
        int msum = _m1+_m2;
        int mdiff = _m1-_m2;
        double ampsum = _ftsum(std::sqrt(ksq));
        double ampdiff = _ftdiff(std::sqrt(ksq));
        double sumcoeff = 0.0;
        double diffcoeff = 0.0;
        if (_m1 >= 0) { // cos term
            if (_m2 >= 0) { // cos term
                sumcoeff = 0.5*cos(msum*phi);
                diffcoeff = 0.5*cos(mdiff*phi);
            } else { // sin term
                sumcoeff = 0.5*sin(msum*phi);
                diffcoeff = -0.5*sin(mdiff*phi);
            }
        } else { // sin term
            if (_m2 >= 0) { // cos term
                sumcoeff = 0.5*sin(msum*phi);
                diffcoeff = 0.5*sin(mdiff*phi);
            } else { // sin term
                sumcoeff = -0.5*cos(msum*phi);
                diffcoeff = 0.5*cos(mdiff*phi);
            }
        }
        std::complex<double> ret = std::complex<double>(0.0, 0.0);
        // contribution from m1+m2 order Hankel transform
        int ipower = msum % 4;
        switch(ipower) {
          case 0:
               ret += std::complex<double>(ampsum*sumcoeff, 0.0);
               break;
          case 1:
               ret += std::complex<double>(0.0, ampsum*sumcoeff);
               break;
          case 2:
               ret += std::complex<double>(-ampsum*sumcoeff, 0.0);
               break;
          case 3:
               ret += std::complex<double>(0.0, -ampsum*sumcoeff);
               break;
        }
        // contribution from m1+m2 order Hankel transform
        ipower = mdiff % 4;
        switch(ipower) {
          case 0:
               ret += std::complex<double>(ampdiff*diffcoeff, 0.0);
               break;
          case 1:
               ret += std::complex<double>(0.0, ampdiff*diffcoeff);
               break;
          case 2:
               ret += std::complex<double>(-ampdiff*diffcoeff, 0.0);
               break;
          case 3:
               ret += std::complex<double>(0.0, -ampdiff*diffcoeff);
               break;
        }

        // if (msum & 1) { // contributes to imaginary component
        //     if (msum & 2) // neg
        //         ret += std::complex<double>(0.0, ampsum*sumcoeff);
        //     else // pos
        //         ret += std::complex<double>(0.0, ampsum*sumcoeff);
        // } else { // contributes to real component
        //     if (msum & 2) // neg
        //         ret += ampsum*sumcoeff;
        //     else
        //         ret += ampsum*sumcoeff;
        // }

        // contribution from m1-m2 order Hankel transform
        // if (mdiff & 1) { // contributes to imaginary component
        //     if (mdiff & 2) // neg
        //         ret += std::complex<double>(0.0, ampdiff*diffcoeff);
        //     else // pos
        //         ret += std::complex<double>(0.0, ampdiff*diffcoeff);
        // } else { // contributes to real component
        //     if (mdiff & 2) // neg
        //         ret += ampdiff*diffcoeff;
        //     else
        //         ret += ampdiff*diffcoeff;
        // }
        return ret;
    }

    class LinearOpticaletIntegrandSum : public std::unary_function<double, double>
    {
    public:
        LinearOpticaletIntegrandSum(int n1, int m1, int n2, int m2, double k) :
            _n1(n1), _m1(m1), _n2(n2), _m2(m2), _k(k) {}
        double operator()(double r) const
        {
            double rpi = M_PI*r;
            return math::cyl_bessel_j(_n1+1, rpi) * math::cyl_bessel_j(_n2+1, rpi)
                * math::cyl_bessel_j(_m1+_m2, r*_k) / r / M_PI / M_PI;
        }
    private:
        int _n1, _m1, _n2, _m2;
        double _k;
    };

    class LinearOpticaletIntegrandDiff : public std::unary_function<double, double>
    {
    public:
        LinearOpticaletIntegrandDiff(int n1, int m1, int n2, int m2, double k) :
            _n1(n1), _m1(m1), _n2(n2), _m2(m2), _k(k) {}
        double operator()(double r) const
        {
            double rpi = M_PI*r;
            return math::cyl_bessel_j(_n1+1, rpi) * math::cyl_bessel_j(_n2+1, rpi)
                * math::cyl_bessel_j(_m1-_m2, r*_k) / r / M_PI / M_PI;
        }
    private:
        int _n1, _m1, _n2, _m2;
        double _k;
    };

    void LinearOpticaletInfo::setupFT() const
    {
        // TODO: should really be integrating out to MOCK_INF here, but that seems to be running
        // into difficulties.  Maybe should continually extend the upper limit until the marginal
        // difference in the integral is smaller than kvalue_accuracy.  Using 2000 as the upper
        // limit for now...
        // Note that the asymptotic formula for a Bessel function doesn't help much here.  An
        // individual Bessel function is bounded by sqrt(2 / (pi r)), so the product of three
        // goes something like 1/r^(3/2).  This integral diverges for the lower limit of r=0
        // though, and also grossly over estimates the highly oscillatory triple Bessel product.

        if (_ftsum.finalized()) return;
        dbg<<"Building LinearOpticalet Hankel transform"<<std::endl;
        dbg<<"(n1,m1,n2,m2) = ("<<_n1<<","<<_m1<<","<<_n2<<","<<_m2<<")"<<std::endl;
        // Do a Hankel transform and store the results in a lookup table.
        double prefactor = 2.0*M_PI*M_PI;
        dbg<<"prefactor = "<<prefactor<<std::endl;

        // We use a cubic spline for the interpolation, which has an error of O(h^4) max(f'''').
        // I have no idea what range the fourth derivative can take for the Hankel transform,
        // so let's take the completely arbitrary value of 10.  (This value was found to be
        // conservative for Sersic, but I haven't investigated here.)
        // 10 h^4 <= xvalue_accuracy
        // h = (xvalue_accuracy/10)^0.25
        double dk = _gsparams->table_spacing * sqrt(sqrt(_gsparams->kvalue_accuracy / 10.));
        double kmax = maxK();
        dbg<<"Using dk = "<<dk<<std::endl;
        dbg<<"Max k = "<<kmax<<std::endl;

        double kmin = 0.0;
        for (double k = kmin; k < kmax; k += dk) {
            LinearOpticaletIntegrandSum Isum(_n1, _m1, _n2, _m2, k);
            LinearOpticaletIntegrandDiff Idiff(_n1, _m1, _n2, _m2, k);

            double upperlimit = 2000.0; // arbitrary, but roughly tested in Mathematica
            integ::IntRegion<double> regsum(0, upperlimit);
            integ::IntRegion<double> regdiff(0, upperlimit);

#if 0
            // Add explicit splits at first several roots of Jn.
            // This tends to make the integral more accurate.
            // First the zeros of J(msum, k r) and J(mdiff, k r)
            if (k != 0.0) {
                for (int s=1; s<=100; ++s) {
                    double root = math::cyl_bessel_j_zero(double(_m1+_m2), s);
                    if (root > k*upperlimit) break;
                    xxdbg<<"root="<<root/k<<std::endl;
                    regsum.addSplit(root/k);
                }
                for (int s=1; s<=100; ++s) {
                    double root = math::cyl_bessel_j_zero(double(_m1-_m2), s);
                    if (root > k*upperlimit) break;
                    xxdbg<<"root="<<root/k<<std::endl;
                    regdiff.addSplit(root/k);
                }
            }
            // Now zeros of J(n1+1, r)
            for (int s=1; s<100; ++s) {
                double root = math::cyl_bessel_j_zero(double(_n1+1), s);
                if (root > upperlimit) break;
                regsum.addSplit(root);
                regdiff.addSplit(root);
            }
            // And finally zeros of J(n2+1, r)
            for (int s=1; s<100; ++s) {
                double root = math::cyl_bessel_j_zero(double(_n2+1), s);
                if (root > upperlimit) break;
                regsum.addSplit(root);
                regdiff.addSplit(root);
            }
#endif

            xxdbg<<"int reg = ("<<0<<",inf)"<<std::endl;

            double valsum = integ::int1d(
                Isum, regsum,
                this->_gsparams->integration_relerr,
                this->_gsparams->integration_abserr);
            valsum *= prefactor;

            double valdiff = integ::int1d(
                Idiff, regdiff,
                this->_gsparams->integration_relerr,
                this->_gsparams->integration_abserr);
            valdiff *= prefactor;

            xdbg<<"ftsum("<<k<<") = "<<valsum<<"   "<<valsum<<std::endl;
            xdbg<<"ftdiff("<<k<<") = "<<valdiff<<"   "<<valdiff<<std::endl;

            _ftsum.addEntry(k, valsum);
            _ftdiff.addEntry(k, valdiff);
        }
        _ftsum.finalize();
        _ftdiff.finalize();
    }
}
