    Info<< "Reading thermophysical properties\n" << endl;

    autoPtr<flameletSMOKEThermo> pThermo
    (
        flameletSMOKEThermo::New(mesh)
    );
    flameletSMOKEThermo& thermo = pThermo();

    volScalarField rho
    (
        IOobject
        (
            "rho",
            runTime.timeName(),
            mesh,
            IOobject::NO_READ,
            IOobject::AUTO_WRITE
        ),
        thermo.rho()
    );


    Info<< "Preparing additional scalar fields\n" << endl <<
           "     + thermo.p()"          << endl <<
           "     + thermo.Z()"          << endl <<
           "     + thermo.Zvar()"       << endl <<
           "     + thermo.chi_st()"	<< endl <<
           "     + thermo.H()"          << endl <<
           "     + thermo.as()"         << endl << endl;

        volScalarField& p       = thermo.p();                   // pressure [Pa]
        volScalarField& Z       = thermo.Z();                   // mixture fraction [-]
        volScalarField& Zvar    = thermo.Zvar();                // variance of mixture fraction [-]
        volScalarField& chi_st  = thermo.chi_st();              // scalar dissipation rate [1/s]
        volScalarField& H       = thermo.H();                   // enthalpy [J/kg]
        volScalarField& as      = thermo.as();                  // mean absorption coefficient [1/m]

	const volScalarField& psi = thermo.psi();



    Info<< "\nReading field U\n" << endl;
    volVectorField U
    (
        IOobject
        (
            "U",
            runTime.timeName(),
            mesh,
            IOobject::MUST_READ,
            IOobject::AUTO_WRITE
        ),
        mesh
    );

    #include "compressibleCreatePhi.H"

    Info<< "Creating turbulence model\n" << endl;
    autoPtr<compressible::RASModel> turbulence
    (
        compressible::RASModel::New
        (
            rho,
            U,
            phi,
            thermo
        )
    );



    Info<< "Creating field dpdt\n" << endl;
    volScalarField dpdt =
        fvc::DDt(surfaceScalarField("phiU", phi/fvc::interpolate(rho)), p);

    Info<< "Creating field kinetic energy K\n" << endl;
    volScalarField K("K", 0.5*magSqr(U));

    Info<< "Reading flameletProperties dictionary\n" << endl;
    IOdictionary flameletsProperties_
    (
        IOobject
        (
            "flameletsProperties",
            U.time().constant(),
            U.db(),
            IOobject::MUST_READ,
            IOobject::NO_WRITE
        )
    );


    dimensionedScalar initialMass = fvc::domainIntegrate(rho);

    //- Monitoring
    Switch monitoring(flameletsProperties_.lookup("monitoring"));

    //- Semi-empirical constants in mixture fraction variance equation
        scalar Cg     = readScalar(flameletsProperties_.lookup("Cg"));
        scalar Cd     = readScalar(flameletsProperties_.lookup("Cd"));

        //- Semi-empirical constant for scalar dissipation rate evaluation
        scalar Cx     = readScalar(flameletsProperties_.lookup("Cx"));

        //- Prandtl number for mixture fraction, variance of mixture fraction and enthalpy diffusion terms
        scalar sigmat_ = readScalar(flameletsProperties_.lookup("sigmat"));
        dimensionedScalar sigmat("sigmat", dimensionSet(0,0,0,0,0,0,0), sigmat_);

        //- Minimum values (to be used for scalar dissipation rate evaluation)
        dimensionedScalar k_small( flameletsProperties_.lookup("k_small") );
        dimensionedScalar epsilon_small( flameletsProperties_.lookup("epsilon_small") );

        //- Environment temperature (non adiabatic consitions)
        dimensionedScalar Tenv ( flameletsProperties_.lookup("Tenv") );

        //- Variance of mixture fraction: on=transport equation | off=algebraic equation
        Switch varianceEquation(flameletsProperties_.lookup("varianceEquation"));

        //- Adiabatic simulation: on=adiabatic | off=non adiabatic
        Switch adiabaticMode(flameletsProperties_.lookup("adiabaticMode"));
        Switch radiationMode(flameletsProperties_.lookup("radiationMode"));

        bool switchQoff = false;

        Info<< "Preparing field Qrad (radiative heat transfer)\n" << endl;
        volScalarField Qrad
        (
                IOobject
                (
                        "Qrad",
                        runTime.timeName(),
                        mesh,
                        IOobject::NO_READ,
                        IOobject::AUTO_WRITE
                ),
                mesh,
                dimensionedScalar("qradiation", dimensionSet(1,-1,-3,0,0,0,0) , 0.0)
        );

        dimensionedScalar sigmaSB("stefan_bolzmann", dimensionSet(1,0,-3,-4,0,0,0), 5.6704e-8);

