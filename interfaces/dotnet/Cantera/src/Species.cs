// This file is part of Cantera. See License.txt in the top-level directory or
// at https://cantera.org/license.txt for license and copyright information.

namespace Cantera;

public class Species : IEquatable<Species>
{
    public string Name { get; }
    public double MolecularWeight { get; }
    public double Charge { get; }

    internal Species(string name, double molecularWeight, double charge)
    {
        Name = name;
        MolecularWeight = molecularWeight;
        Charge = charge;
    }

    public bool Equals(Species? other) =>
        StringComparer.OrdinalIgnoreCase.Equals(Name, other?.Name);

    public override bool Equals(object? obj) =>
        obj is Species other && Equals(other);

    public override int GetHashCode() =>
        StringComparer.OrdinalIgnoreCase.GetHashCode(Name);
}