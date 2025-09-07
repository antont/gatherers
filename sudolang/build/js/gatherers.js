// An Gatherers - JavaScript Implementation
// Transpiled from SudoLang specification

// === UTILITY CLASSES ===
class Vec2 {
    constructor(x = 0, y = 0) {
        this.x = x;
        this.y = y;
    }
    
    static fromAngle(angleDegrees) {
        const radians = angleDegrees * Math.PI / 180;
        return new Vec2(Math.cos(radians), Math.sin(radians));
    }
    
    angle() {
        return Math.atan2(this.y, this.x) * 180 / Math.PI;
    }
    
    multiply(scalar) {
        return new Vec2(this.x * scalar, this.y * scalar);
    }
    
    add(other) {
        return new Vec2(this.x + other.x, this.y + other.y);
    }
    
    distance(other) {
        const dx = this.x - other.x;
        const dy = this.y - other.y;
        return Math.sqrt(dx * dx + dy * dy);
    }
}

// === CONFIGURATION ===
const Config = {
    world: {
        width: 800,
        height: 600,
        wrapBoundaries: true
    },
    
    agents: {
        count: 16,
        size: 20,
        speed: 100, // pixels per second
        spawnY: 100,
        color: "lightgray"
    },
    
    resources: {
        count: 80,
        size: 10,
        color: "darkred"
    },
    
    behavior: {
        pickupCooldown: 0.1, // seconds
        turnAngleRange: 90   // degrees (±45°)
    }
};

// === UTILITY FUNCTIONS ===
function random(min, max) {
    return Math.random() * (max - min) + min;
}

function distance(pos1, pos2) {
    return pos1.distance(pos2);
}

// === ENTITY CLASSES ===
class Agent {
    constructor(position, velocity) {
        this.position = position;
        this.velocity = velocity;
        this.carrying = null;
        this.cooldown = 0;
    }
    
    move(deltaTime) {
        // Move forward
        const movement = this.velocity.multiply(deltaTime);
        this.position = this.position.add(movement);
        
        // Wrap around screen boundaries
        const halfWidth = Config.world.width / 2;
        const halfHeight = Config.world.height / 2;
        
        if (this.position.x < -halfWidth) this.position.x = halfWidth;
        if (this.position.x > halfWidth) this.position.x = -halfWidth;
        if (this.position.y < -halfHeight) this.position.y = halfHeight;
        if (this.position.y > halfHeight) this.position.y = -halfHeight;
    }
    
    interact(resource) {
        if (this.cooldown > 0) return; // Still in cooldown
        
        if (this.carrying === null) {
            // Pick up resource
            this.carrying = resource;
            resource.pickedUp = true;
            this.turnAround();
            this.cooldown = Config.behavior.pickupCooldown;
        } else {
            // Drop carried resource at current location
            this.carrying.position = new Vec2(this.position.x, this.position.y);
            this.carrying.pickedUp = false;
            this.carrying = null;
            this.turnAround();
            this.cooldown = Config.behavior.pickupCooldown;
        }
    }
    
    turnAround() {
        // Turn approximately 180° with random variation
        const currentAngle = this.velocity.angle();
        const randomOffset = random(-Config.behavior.turnAngleRange/2, Config.behavior.turnAngleRange/2);
        const newAngle = currentAngle + 180 + randomOffset;
        this.velocity = Vec2.fromAngle(newAngle).multiply(Config.agents.speed);
    }
}

class Resource {
    constructor(position) {
        this.position = position;
        this.pickedUp = false;
    }
    
    getVisualPosition(carrier) {
        return this.pickedUp ? carrier.position : this.position;
    }
}

// === SIMULATION CLASS ===
class Simulation {
    constructor() {
        this.agents = [];
        this.resources = [];
        this.time = 0;
        this.paused = false;
    }
    
    initialize() {
        this.agents = [];
        this.resources = [];
        this.time = 0;
        
        // Spawn agents in a line
        const stepSize = Config.world.width / Config.agents.count;
        for (let i = 0; i < Config.agents.count; i++) {
            const x = -Config.world.width/2 + i * stepSize;
            const randomAngle = random(0, 360);
            const velocity = Vec2.fromAngle(randomAngle).multiply(Config.agents.speed);
            
            const agent = new Agent(
                new Vec2(x, Config.agents.spawnY),
                velocity
            );
            this.agents.push(agent);
        }
        
        // Spawn resources randomly
        for (let i = 0; i < Config.resources.count; i++) {
            const position = new Vec2(
                random(-Config.world.width/2, Config.world.width/2),
                random(-Config.world.height/2, Config.world.height/2)
            );
            const resource = new Resource(position);
            this.resources.push(resource);
        }
    }
    
    update(deltaTime) {
        if (this.paused) return;
        
        this.time += deltaTime;
        
        // Update agent cooldowns
        this.agents.forEach(agent => {
            if (agent.cooldown > 0) {
                agent.cooldown -= deltaTime;
            }
        });
        
        // Move all agents
        this.agents.forEach(agent => agent.move(deltaTime));
        
        // Check collisions between agents and available resources
        this.agents.forEach(agent => {
            const availableResources = this.resources.filter(r => !r.pickedUp);
            const nearbyResource = this.findNearestResource(agent.position, availableResources, 15);
            
            if (nearbyResource) {
                agent.interact(nearbyResource);
            }
        });
    }
    
    findNearestResource(position, resources, maxDistance) {
        return resources.find(resource => 
            distance(position, resource.position) < maxDistance
        );
    }
    
    render(ctx) {
        // Clear canvas
        ctx.fillStyle = '#5f97d4'; // Background color
        ctx.fillRect(0, 0, Config.world.width, Config.world.height);
        
        // Transform coordinate system to center origin
        ctx.save();
        ctx.translate(Config.world.width / 2, Config.world.height / 2);
        
        // Render resources that are not picked up
        this.resources.forEach(resource => {
            if (!resource.pickedUp) {
                ctx.fillStyle = Config.resources.color;
                ctx.fillRect(
                    resource.position.x - Config.resources.size / 2,
                    resource.position.y - Config.resources.size / 2,
                    Config.resources.size,
                    Config.resources.size
                );
            }
        });
        
        // Render agents and carried resources
        this.agents.forEach(agent => {
            // Render agent
            ctx.fillStyle = Config.agents.color;
            ctx.fillRect(
                agent.position.x - Config.agents.size / 2,
                agent.position.y - Config.agents.size / 2,
                Config.agents.size,
                Config.agents.size
            );
            
            // Render carried resource
            if (agent.carrying) {
                ctx.fillStyle = Config.resources.color;
                ctx.fillRect(
                    agent.position.x - Config.resources.size / 2,
                    agent.position.y - Config.resources.size / 2,
                    Config.resources.size,
                    Config.resources.size
                );
            }
        });
        
        ctx.restore();
    }
    
    reset() {
        this.initialize();
    }
    
    pause() {
        this.paused = true;
    }
    
    resume() {
        this.paused = false;
    }
    
    togglePause() {
        this.paused = !this.paused;
        return this.paused;
    }
}

// === MAIN APPLICATION ===
let simulation;
let canvas;
let ctx;
let lastTime = 0;
let animationId;

function init() {
    canvas = document.getElementById('canvas');
    ctx = canvas.getContext('2d');
    
    simulation = new Simulation();
    simulation.initialize();
    
    // Start the game loop
    lastTime = performance.now();
    gameLoop();
}

function gameLoop(currentTime = performance.now()) {
    const deltaTime = (currentTime - lastTime) / 1000; // Convert to seconds
    lastTime = currentTime;
    
    simulation.update(deltaTime);
    simulation.render(ctx);
    
    animationId = requestAnimationFrame(gameLoop);
}

function togglePause() {
    const isPaused = simulation.togglePause();
    const statusElement = document.getElementById('status');
    statusElement.textContent = isPaused ? 'Paused' : 'Running...';
}

// Initialize when page loads
window.addEventListener('load', init);

// Handle window resize
window.addEventListener('resize', () => {
    // Could add responsive canvas sizing here if needed
});

// Export for global access
window.simulation = simulation;
window.togglePause = togglePause;
