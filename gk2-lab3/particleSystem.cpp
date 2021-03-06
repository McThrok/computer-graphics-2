#include "particleSystem.h"
#include "dxDevice.h"
#include "exceptions.h"

using namespace mini;
using namespace gk2;
using namespace utils;
using namespace DirectX;
using namespace std;

const D3D11_INPUT_ELEMENT_DESC ParticleVertex::Layout[4] =
{
	{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	{ "TEXCOORD", 0, DXGI_FORMAT_R32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	{ "TEXCOORD", 1, DXGI_FORMAT_R32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	{ "TEXCOORD", 2, DXGI_FORMAT_R32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0 }
};

const XMFLOAT3 ParticleSystem::EMITTER_DIR = XMFLOAT3(0.0f, 1.0f, 0.0f);
const float ParticleSystem::TIME_TO_LIVE = 4.0f;
const float ParticleSystem::EMISSION_RATE = 10.0f;
const float ParticleSystem::MAX_ANGLE = XM_PIDIV2 / 9.0f;
const float ParticleSystem::MIN_VELOCITY = 0.2f;
const float ParticleSystem::MAX_VELOCITY = 0.33f;
const float ParticleSystem::PARTICLE_SIZE = 0.08f;
const float ParticleSystem::PARTICLE_SCALE = 1.0f;
const float ParticleSystem::MIN_ANGLE_VEL = -XM_PI;
const float ParticleSystem::MAX_ANGLE_VEL = XM_PI;
const int ParticleSystem::MAX_PARTICLES = 500;

const unsigned int ParticleSystem::STRIDE = sizeof(ParticleVertex);
const unsigned int ParticleSystem::OFFSET = 0;

ParticleEffect::ParticleEffect(dx_ptr<ID3D11VertexShader>&& vs, dx_ptr<ID3D11GeometryShader>&& gs, dx_ptr<ID3D11PixelShader>&& ps,
	const ConstantBuffer<DirectX::XMFLOAT4X4, 2> cbView, const ConstantBuffer<DirectX::XMFLOAT4X4>& cbProj,
	const dx_ptr<ID3D11SamplerState>& sampler, dx_ptr<ID3D11ShaderResourceView>&& colorMap,
	dx_ptr<ID3D11ShaderResourceView>&& opacityMap)
	: StaticEffect(BasicEffect(move(vs), move(ps)), GeometryShaderComponent(move(gs)), VSConstantBuffers{ cbView },
		GSConstantBuffers{ cbProj }, PSSamplers{ sampler }, PSShaderResources{ colorMap, opacityMap })
{
}

ParticleSystem::ParticleSystem(const DxDevice& device, const ConstantBuffer<DirectX::XMFLOAT4X4, 2> cbView,
	const ConstantBuffer<DirectX::XMFLOAT4X4>& cbProj, const dx_ptr<ID3D11SamplerState>& sampler,
	DirectX::XMFLOAT3 emmiterPosition)
	: m_emitterPos(emmiterPosition), m_particlesToCreate(0.0f), m_particlesCount(0), m_random(random_device{}())
{
	m_vertices = device.CreateVertexBuffer<ParticleVertex>(MAX_PARTICLES);
	auto vsCode = device.LoadByteCode(L"particleVS.cso");
	auto gsCode = device.LoadByteCode(L"particleGS.cso");
	auto psCode = device.LoadByteCode(L"particlePS.cso");
	m_effect = ParticleEffect(device.CreateVertexShader(vsCode), device.CreateGeometryShader(gsCode),
		device.CreatePixelShader(psCode), cbView, cbProj, sampler,
		device.CreateShaderResourceView(L"resources/textures/smoke.png"),
		device.CreateShaderResourceView(L"resources/textures/smokecolors.png"));
	m_inputLayout = device.CreateInputLayout<ParticleVertex>(vsCode);
}

void ParticleSystem::Update(const dx_ptr<ID3D11DeviceContext>& context, float dt, DirectX::XMFLOAT4 cameraPosition)
{
	for (auto it = m_particles.begin(); it != m_particles.end(); )
	{
		UpdateParticle(*it, dt);
		auto prev = it++;
		if (prev->Vertex.Age >= TIME_TO_LIVE)
		{
			m_particles.erase(prev);
			--m_particlesCount;
		}
	}
	m_particlesToCreate += dt * EMISSION_RATE;
	while (m_particlesToCreate >= 1.0f)
	{
		--m_particlesToCreate;
		if (m_particlesCount < MAX_PARTICLES)
		{
			AddNewParticle();
			++m_particlesCount;
		}
	}
	UpdateVertexBuffer(context, cameraPosition);
}

void ParticleSystem::Render(const dx_ptr<ID3D11DeviceContext>& context) const
{
	m_effect.Begin(context);
	ID3D11Buffer* vb[1] = { m_vertices.get() };
	context->IASetVertexBuffers(0, 1, vb, &STRIDE, &OFFSET);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
	context->IASetInputLayout(m_inputLayout.get());
	context->Draw(m_particlesCount, 0);
}

XMFLOAT3 ParticleSystem::RandomVelocity()
{
	static const uniform_real_distribution<float> angleDist(0, XM_2PI);
	static const uniform_real_distribution<float> magnitudeDist(0, tan(MAX_ANGLE));
	static const uniform_real_distribution<float> velDist(MIN_VELOCITY, MAX_VELOCITY);
	float angle = angleDist(m_random);
	float magnitude = magnitudeDist(m_random);
	XMFLOAT3 v{ cos(angle)*magnitude, 1.0f, sin(angle)*magnitude };

	auto velocity = XMLoadFloat3(&v);
	auto len = velDist(m_random);
	velocity = len * XMVector3Normalize(velocity);
	XMStoreFloat3(&v, velocity);
	return v;
}

void ParticleSystem::AddNewParticle()
{
	static const uniform_real_distribution<float> anglularVelDist(MIN_ANGLE_VEL, MAX_ANGLE_VEL);
	Particle p;
	// TODO : 2.26 Setup initial particle properties
	p.Vertex.Age = 0;
	p.Vertex.Size = PARTICLE_SIZE;
	p.Vertex.Pos = m_emitterPos;
	p.Vertex.Angle = 0;
	p.Velocities.Velocity = RandomVelocity();
	p.Velocities.AngularVelocity = anglularVelDist(m_random);

	m_particles.push_back(p);
}

void ParticleSystem::UpdateParticle(Particle& p, float dt)
{
	// TODO : 2.26 Update particle properties
	p.Vertex.Age += dt;
	p.Vertex.Pos.x += p.Velocities.Velocity.x * dt;
	p.Vertex.Pos.y += p.Velocities.Velocity.y * dt;
	p.Vertex.Pos.z += p.Velocities.Velocity.z * dt;
	p.Vertex.Angle += p.Velocities.AngularVelocity * dt;
	p.Vertex.Size += PARTICLE_SCALE * PARTICLE_SIZE * dt;
}

void ParticleSystem::UpdateVertexBuffer(const dx_ptr<ID3D11DeviceContext>& context, 
	DirectX::XMFLOAT4 cameraPosition)
{
	XMFLOAT4 cameraTarget(0.0f, 0.0f, 0.0f, 1.0f);

	vector<ParticleVertex> vertices(MAX_PARTICLES);
	// TODO : 2.27 Copy particles to a vector and sort them
	std::list<Particle>::iterator it;
	size_t ind = 0;
	for (it = m_particles.begin(); it != m_particles.end(); it++, ind++)
		vertices[ind] = it->Vertex;

	for (auto &p : m_particles)
	std::sort(vertices.begin(), vertices.begin() + m_particlesCount, 
		[&cameraPosition](const ParticleVertex & a, const ParticleVertex & b) -> bool
		{
			float x1 = a.Pos.x - cameraPosition.x;
			float y1 = a.Pos.y - cameraPosition.y;
			float z1 = a.Pos.z - cameraPosition.z;
			float d1 = x1 * x1 + y1 * y1 + z1 * z1;

			float x2 = b.Pos.x - cameraPosition.x;
			float y2 = b.Pos.y - cameraPosition.y;
			float z2 = b.Pos.z - cameraPosition.z;
			float d2 = x2 * x2 + y2 * y2 + z2 * z2;

			return  d1 > d2;
		});

	D3D11_MAPPED_SUBRESOURCE resource;
	auto hr = context->Map(m_vertices.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &resource);
	if (FAILED(hr))
		THROW_DX(hr);
	memcpy(resource.pData, vertices.data(), MAX_PARTICLES * sizeof(ParticleVertex));
	context->Unmap(m_vertices.get(), 0);
}