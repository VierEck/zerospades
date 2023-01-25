/*
 Copyright (c) 2013 yvt

 This file is part of OpenSpades.

 OpenSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 OpenSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with OpenSpades.	 If not, see <http://www.gnu.org/licenses/>.

 */

namespace spades {
	class ViewRifleSkin : BasicViewWeapon {
		private AudioDevice@ audioDevice;
		private Model@ gunModel;
		private Model@ magazineModel;
		private Model@ sightModel1;
		private Model@ sightModel2;

		private AudioChunk@ fireSound;
		private AudioChunk@ fireFarSound;
		private AudioChunk@ fireStereoSound;
		private AudioChunk@ fireSmallReverbSound;
		private AudioChunk@ fireLargeReverbSound;
		private AudioChunk@ reloadSound;

		// Constants

		// Attachment Points
		private Vector3 magazineAttachment = Vector3(0, 0, 0);
		private Vector3 rearSightAttachment = Vector3(0.0125F, -8.0F, -4.5F);
		private Vector3 frontSightAttachment = Vector3(0.025F, 16.75F, -4.575F);

		// Scales
		private float globalScale = 0.033F;
		private float magazineScale = 0.5F;
		private float rearSightScale = 0.025F;
		private float frontSightScale = 0.05F;

		// A bunch of springs.
		private ViewWeaponSpring recoilVerticalSpring = ViewWeaponSpring(200, 24);
		private ViewWeaponSpring recoilBackSpring = ViewWeaponSpring(100, 16);
		private ViewWeaponSpring recoilRotationSpring = ViewWeaponSpring(50, 8);
		private ViewWeaponSpring horizontalSwingSpring = ViewWeaponSpring(100, 12);
		private ViewWeaponSpring verticalSwingSpring = ViewWeaponSpring(100, 12);
		private ViewWeaponSpring reloadPitchSpring = ViewWeaponSpring(150, 12, 0);
		private ViewWeaponSpring reloadRollSpring = ViewWeaponSpring(150, 16, 0);
		private ViewWeaponSpring reloadOffsetSpring = ViewWeaponSpring(150, 12, 0);
		private ViewWeaponSpring sprintSpring = ViewWeaponSpring(100, 10, 0);
		private ViewWeaponSpring raiseSpring = ViewWeaponSpring(200, 20, 1);

		// A bunch of events.
		private ViewWeaponEvent magazineTouched = ViewWeaponEvent();
		private ViewWeaponEvent magazineRemoved = ViewWeaponEvent();
		private ViewWeaponEvent magazineInserted = ViewWeaponEvent();
		private ViewWeaponEvent chargingHandlePulled = ViewWeaponEvent();

		// A bunch of states.
		private double lastSprintState = 0;
		private double lastRaiseState = 0;

		// I need trueReloadProgress because reloadProgress is choppy.
		private double trueReloadProgress = 2.5;

		Matrix4 AdjustToReload(Matrix4 mat) {
			if (trueReloadProgress < 0.6) {
				reloadPitchSpring.desired = 0.6;
				reloadRollSpring.desired = 0.6;
			} else if (trueReloadProgress < 0.9) {
				reloadPitchSpring.desired = 0;
				reloadRollSpring.desired = -0.6;
			} else {
				reloadPitchSpring.desired = 0;
				reloadRollSpring.desired = 0;
			}

			if (magazineTouched.WasActivated()) {
				magazineTouched.Acknowledge();
				reloadPitchSpring.velocity = 4;
			}

			if (magazineRemoved.WasActivated()) {
				magazineRemoved.Acknowledge();
				reloadPitchSpring.velocity = -4;
			}

			if (magazineInserted.WasActivated()) {
				magazineInserted.Acknowledge();
				reloadPitchSpring.velocity = 8;
			}

			if (chargingHandlePulled.WasActivated()) {
				chargingHandlePulled.Acknowledge();
				reloadPitchSpring.velocity = 5;
				reloadOffsetSpring.velocity = 1;
			}

			float sp = 1.0F - AimDownSightStateSmooth;

			mat *= CreateEulerAnglesMatrix(Vector3(0, 0.6, 0) * reloadRollSpring.position * sp);
			mat *= CreateEulerAnglesMatrix(Vector3(-0.25, 0, 0) * reloadPitchSpring.position * sp);
			mat *= CreateTranslateMatrix(Vector3(0, -1, 0) * reloadOffsetSpring.position * sp);
			mat *= CreateTranslateMatrix(Vector3(0, 0, -globalScale) * reloadPitchSpring.position * sp);

			return mat;
		}

		Vector3 GetMagazineOffset() {
			Vector3 offsetPos = Vector3(0, -6, 8);

			if (trueReloadProgress < 0.2) {
				return magazineAttachment;
			} else if (trueReloadProgress < 0.25) {
				magazineRemoved.Activate();
				float per = Min(1.0, (trueReloadProgress-0.2) / (0.25-0.2));
				return Mix(magazineAttachment, offsetPos, SmoothStep(per));
			} else if (trueReloadProgress < 0.4) {
				return offsetPos;
			} else if (trueReloadProgress < 0.5) {
				float per = Min(1.0, (trueReloadProgress-0.4) / (0.5-0.4));
				return Mix(offsetPos, magazineAttachment, SmoothStep(per));
			} else {
				magazineInserted.Activate();
				return magazineAttachment;
			}
		}

		Vector3 GetLeftHandOffset() {
			Vector3 leftHandOffset = Vector3(1, 6, 1);
			Vector3 magazineOffset = GetMagazineOffset() + Vector3(0, 0, 4);

			if (trueReloadProgress < 0.1) {
				float per = Min(1.0, trueReloadProgress / 0.1);
				return Mix(leftHandOffset, magazineOffset, SmoothStep(per));
			} else if (trueReloadProgress < 0.6) {
				magazineTouched.Activate();
				return magazineOffset;
			} else if (trueReloadProgress < 1.0) {
				float per = Min(1.0, (trueReloadProgress-0.6) / (1.0-0.9));
				return Mix(magazineOffset, leftHandOffset, SmoothStep(per));
			} else {
				return leftHandOffset;
			}
		}

		Vector3 GetRightHandOffset() {
			Vector3 rightHandOffset = Vector3(0, -8, 2);
			Vector3 chargingHandleOffset = Vector3(-3, -4, -5);

			// sprint animation
			rightHandOffset -= Vector3(0, 3, 0.5) * sprintSpring.position;
			chargingHandleOffset -= Vector3(0, 2, -3) * sprintSpring.position;

			Vector3 handlePullingOffset = chargingHandleOffset + Vector3(-1, -4, 0);

			if (trueReloadProgress < 0.7) {
				return rightHandOffset;
			} else if (trueReloadProgress < 0.8) {
				float per = Min(1.0, (trueReloadProgress-0.7) / (0.8-0.7));
				return Mix(rightHandOffset, chargingHandleOffset, SmoothStep(per));
			} else if (trueReloadProgress < 0.82) {
				return chargingHandleOffset;
			} else if (trueReloadProgress < 0.87) {
				chargingHandlePulled.Activate();
				float per = Min(1.0, (trueReloadProgress-0.82) / (0.87-0.82));
				return Mix(chargingHandleOffset, handlePullingOffset, SmoothStep(per));
			} else if (trueReloadProgress < 1.0) {
				float per = Min(1.0, (trueReloadProgress-0.87) / (1.0-0.87));
				return Mix(handlePullingOffset, rightHandOffset, SmoothStep(per));
			} else {
				return rightHandOffset;
			}
		}

		ViewRifleSkin(Renderer@ r, AudioDevice@ dev) {
			super(r);
			@audioDevice = dev;
			@gunModel = renderer.RegisterModel("Models/Weapons/Rifle/WeaponNoMagazine.kv6");
			@magazineModel = renderer.RegisterModel("Models/Weapons/Rifle/Magazine.kv6");
			@sightModel1 = renderer.RegisterModel("Models/Weapons/Rifle/Sight1.kv6");
			@sightModel2 = renderer.RegisterModel("Models/Weapons/Rifle/Sight2.kv6");

			@fireSound = dev.RegisterSound("Sounds/Weapons/Rifle/FireLocal.opus");
			@fireFarSound = dev.RegisterSound("Sounds/Weapons/Rifle/FireFar.opus");
			@fireStereoSound = dev.RegisterSound("Sounds/Weapons/Rifle/FireStereo.opus");
			@reloadSound = dev.RegisterSound("Sounds/Weapons/Rifle/ReloadLocal.opus");

			@fireSmallReverbSound = dev.RegisterSound("Sounds/Weapons/Rifle/V2AmbienceSmall.opus");
			@fireLargeReverbSound = dev.RegisterSound("Sounds/Weapons/Rifle/V2AmbienceLarge.opus");

			@scopeImage = renderer.RegisterImage("Gfx/Rifle.png");

			raiseSpring.position = 1;
		}

		void Update(float dt) {
			BasicViewWeapon::Update(dt);

			recoilVerticalSpring.damping = Mix(16, 24, AimDownSightState);
			recoilBackSpring.damping = Mix(12, 20, AimDownSightState);
			recoilRotationSpring.damping = Mix(8, 16, AimDownSightState);

			recoilVerticalSpring.Update(dt);
			recoilBackSpring.Update(dt);
			recoilRotationSpring.Update(dt);

			horizontalSwingSpring.velocity += swing.x * 60 * dt * 2;
			verticalSwingSpring.velocity += swing.z * 60 * dt * 2;
			horizontalSwingSpring.Update(dt);
			verticalSwingSpring.Update(dt);

			reloadPitchSpring.Update(dt);
			reloadRollSpring.Update(dt);
			reloadOffsetSpring.Update(dt);
			trueReloadProgress += dt / 2.5;

			sprintSpring.Update(dt);
			raiseSpring.Update(dt);

			bool isSprinting;
			if (sprintState >= 1)
				isSprinting = true;
			else if (sprintState > lastSprintState)
				isSprinting = true;
			else if (sprintState < lastSprintState)
				isSprinting = false;
			else if (sprintState <= 0)
				isSprinting = false;
			else
				isSprinting = false;
			lastSprintState = sprintState;
			sprintSpring.desired = isSprinting ? 1 : 0;

			bool isRaised;
			if (raiseState >= 1)
				isRaised = true;
			else if (raiseState > lastRaiseState)
				isRaised = true;
			else if (raiseState < lastRaiseState)
				isRaised = false;
			else if (raiseState <= 0)
				isRaised = false;
			else
				isRaised = false;
			lastRaiseState = raiseState;
			raiseSpring.desired = isRaised ? 0 : 1;
		}

		void WeaponFired() {
			BasicViewWeapon::WeaponFired();

			if (!IsMuted) {
				Vector3 origin = Vector3(0.4F, -0.3F, 0.5F);
				AudioParam param;
				param.referenceDistance = 4.0F;
				param.volume = 1.0F;
				audioDevice.PlayLocal(fireFarSound, origin, param);
				param.referenceDistance = 1.0F;
				audioDevice.PlayLocal(fireStereoSound, origin, param);

				param.volume = 8.0F * environmentRoom;
				audioDevice.PlayLocal((environmentSize < 0.5F)
					? fireSmallReverbSound : fireLargeReverbSound,
					origin, param);
			}

			recoilVerticalSpring.velocity += 1.5;
			recoilBackSpring.velocity += 1.5;
			recoilRotationSpring.velocity += (GetRandom() * 2 - 1);
		}

		void ReloadingWeapon() {
			magazineTouched.Reset();
			magazineRemoved.Reset();
			magazineInserted.Reset();
			chargingHandlePulled.Reset();

			if (!IsMuted) {
				Vector3 origin = Vector3(0.4F, -0.3F, 0.5F);
				AudioParam param;
				param.volume = 0.5F;
				audioDevice.PlayLocal(reloadSound, origin, param);
			}

			trueReloadProgress = 0;
		}

		// (override BasicViewWeapon::GetZPos())
		float GetZPos() { return 0.2F - AimDownSightStateSmooth * 0.052F; }

		// (override BasicViewWeapon::GetViewWeaponMatrix())
		Matrix4 GetViewWeaponMatrix() {
			Matrix4 mat;

			// sprint animation
			mat = CreateEulerAnglesMatrix(Vector3(0.3F, -0.1F, -0.55F) * sprintSpring.position) * mat;
			mat = CreateTranslateMatrix(Vector3(0.23F, -0.05F, 0.15F) * sprintSpring.position) * mat;

			// raise gun animation
			mat = CreateRotateMatrix(Vector3(0, 0, 1), raiseSpring.position * -1.3F) * mat;
			mat = CreateRotateMatrix(Vector3(0, 1, 0), raiseSpring.position * 0.2F) * mat;
			mat = CreateTranslateMatrix(Vector3(0.1F, -0.3F, 0.1F) * raiseSpring.position) * mat;

			float sp = 1.0F - AimDownSightStateSmooth;

			// recoil animation
            Vector3 recoilRot = Vector3(-1.0, 0.3, 0.3) * recoilVerticalSpring.position;
            Vector3 recoilOffset = Vector3(0, 0, -0.1) * recoilVerticalSpring.position * sp;
            recoilOffset -= Vector3(0, 0.75, 0) * recoilBackSpring.position;
            mat = CreateEulerAnglesMatrix(recoilRot * sp) * mat;
            mat = mat * CreateTranslateMatrix(recoilOffset);

			Vector3 trans(0, 0, 0);
			trans += Vector3(-0.13F * sp, 0.5F, GetZPos());
			trans += swing * sp;
			mat = CreateTranslateMatrix(trans) * mat;

			// twist the gun when strafing
			Vector3 swingRot(0, 0, 0);
			swingRot.z += 2.0F * horizontalSwingSpring.position;
			swingRot.x -= 2.0F * verticalSwingSpring.position;
			mat = mat * CreateEulerAnglesMatrix(swingRot * sp);

			Vector3 pivot = Vector3(0.025F, 0.0F, 0.025F);
			Vector3 sightPos = (frontSightAttachment - pivot) * globalScale;
			mat = AdjustToAlignSight(mat, sightPos, AimDownSightStateSmooth);
			mat = AdjustToReload(mat);

			return mat;
		}

		// IWeaponSkin3 (override BasicViewWeapon::{get_MuzzlePosition, get_CaseEjectPosition})
		Vector3 MuzzlePosition { get { return eyeMatrix * GetViewWeaponMatrix() * Vector3(0.0F, 0.7F, -0.075F); } }
		Vector3 CaseEjectPosition { get { return eyeMatrix * GetViewWeaponMatrix() * Vector3(0.0F, -0.1F, -0.05F); } }

		void Draw2D() {
			BasicViewWeapon::Draw2D();
		}

		void AddToScene() {
			if (cg_pngScope.BoolValue and AimDownSightStateSmooth > 0.99F) {
				LeftHandPosition = Vector3(0.0F, 0.0F, 0.0F);
				RightHandPosition = Vector3(0.0F, 0.0F, 0.0F);
				return;
			}

			Matrix4 mat = GetViewWeaponMatrix()
				* CreateScaleMatrix(globalScale);

			Vector3 leftHand, rightHand;
			leftHand = mat * GetLeftHandOffset();
			rightHand = mat * GetRightHandOffset();

			ModelRenderParam param;
			param.depthHack = true;

			Matrix4 weapMatrix = eyeMatrix * mat;

			// draw weapon
			param.matrix = weapMatrix
				* CreateScaleMatrix(0.5F)
				* CreateTranslateMatrix(-0.5F, 0.0F, 0.0F);
			renderer.AddModel(gunModel, param);

			// draw sights
			param.matrix = weapMatrix
				* CreateTranslateMatrix(rearSightAttachment)
				* CreateScaleMatrix(rearSightScale);
			renderer.AddModel(sightModel1, param); // rear

			param.matrix = weapMatrix
				* CreateTranslateMatrix(frontSightAttachment)
				* CreateScaleMatrix(frontSightScale);
			renderer.AddModel(sightModel2, param); // front pin

			// draw magazine
			param.matrix = weapMatrix
				* CreateTranslateMatrix(GetMagazineOffset());
			renderer.AddModel(magazineModel, param);

			LeftHandPosition = leftHand;
			RightHandPosition = rightHand;
		}
	}

	IWeaponSkin@ CreateViewRifleSkin(Renderer@ r, AudioDevice@ dev) {
		return ViewRifleSkin(r, dev);
	}
}