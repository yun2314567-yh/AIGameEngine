// Example raw-buffer compute contract. u0 carries count followed by values;
// u1 receives twice each input plus one. Dispatch ceil(count / 64) groups.
[[vk::binding(48, 0)]] RWByteAddressBuffer Input : register(u0);
[[vk::binding(49, 0)]] RWByteAddressBuffer Output : register(u1);

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint count = Input.Load(0);
    if (id.x < count) Output.Store(id.x * 4, Input.Load((id.x + 1) * 4) * 2 + 1);
}
