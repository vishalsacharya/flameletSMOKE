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
            IOobject::READ_IF_PRESENT,
            IOobject::AUTO_WRITE
        ),
        thermo.rho()
    );

    Info<< "Preparing additional scalar fields\n" << endl <<
    	   "     + thermo.p()" 		<< endl <<
    	   "     + thermo.Z()" 		<< endl <<
    	   "     + thermo.Zvar()" 	<< endl <<
    	   "     + thermo.chi_st()" << endl <<
    	   "     + thermo.H()" 		<< endl <<
    	   "     + thermo.as()" 	<< endl << endl;

	volScalarField& p 		= thermo.p();			// pressure [Pa]
	volScalarField& Z     	= thermo.Z();			// mixture fraction [-]
	volScalarField& Zvar  	= thermo.Zvar();		// variance of mixture fraction [-]
	volScalarField& chi_st 	= thermo.chi_st();		// scalar dissipation rate [1/s]
	volScalarField& H  		= thermo.H();    		// enthalpy [J/kg]
	volScalarField& as  	= thermo.as();   		// mean absorption coefficient [1/m]

    Info<< "Reading field U\n" << endl;
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

    #if OPENFOAM_VERSION >= 40
    
    	pressureControl pressureControl(p, rho, simple.dict());
    	mesh.setFluxRequired(p.name());
    
    #else

	label pRefCell = 0;
	scalar pRefValue = 0.0;
	setRefCell(p, simple.dict(), pRefCell, pRefValue);

	dimensionedScalar pMin
	(
		mesh.solutionDict().subDict("SIMPLE").lookup("pMin")
	);

	dimensionedScalar rhoMax(simple.dict().lookup("rhoMax"));
	dimensionedScalar rhoMin(simple.dict().lookup("rhoMin"));

    #endif

    #if OPENFOAM_VERSION < 40

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

    #else

    Info << "Creating turbulence model.\n" << nl;
    autoPtr<compressible::turbulenceModel> turbulence
    (
      compressible::turbulenceModel::New
      (
        rho,
        U,
        phi,
        thermo
      )
    );

    #endif

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

        #if OPENFOAM_VERSION >= 40
        #include "createMRF.H"
        #endif

        const volScalarField& psi = thermo.psi();
